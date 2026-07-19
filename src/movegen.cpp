// Catalyst is a UCI compliant chess engine
// Copyright (C) 2026 Anany Tanwar

// Catalyst is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Catalyst is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "movegen.h"

#include "bitboard.h"

namespace Catalyst {

// Emits all four promotion moves (queen/rook/bishop/knight) for a
// non-capture generation, or just queen for captures-only generation -
// under-promotions are rarely useful in a capture, and skipping them here
// keeps the CAPTURES move count (used e.g. in quiescence search) smaller
// without losing any moves that matter in practice.
template <GenType GT> static FORCE_INLINE Move *make_promotions(Move *list, Square from, Square to)
{
    *list++ = make_move(from, to, MT_PROMOTION, QUEEN);
    if constexpr (GT == QUIETS || GT == ALL_MOVES)
    {
        *list++ = make_move(from, to, MT_PROMOTION, ROOK);
        *list++ = make_move(from, to, MT_PROMOTION, BISHOP);
        *list++ = make_move(from, to, MT_PROMOTION, KNIGHT);
    }
    return list;
}

// Pawns get their own dedicated generator since their movement (forward
// push, double push, diagonal-only captures, en passant, promotion) is
// structurally unlike every other piece type's "look up attacks table"
// pattern. `target` is the set of squares this generation call is allowed
// to land on - lets the same function serve full generation, captures-
// only, quiets-only, and evasion-restricted (block/capture-the-checker)
// generation through one shared implementation.
template <Color Us, GenType GT>
static FORCE_INLINE Move *generate_pawn_moves(const Board &board, Move *list, Bitboard target)
{
    constexpr Color     Them    = ~Us;
    constexpr Direction Up      = Us == WHITE ? NORTH : SOUTH;
    constexpr Direction UpLeft  = Us == WHITE ? NORTH_WEST : SOUTH_EAST;
    constexpr Direction UpRight = Us == WHITE ? NORTH_EAST : SOUTH_WEST;
    constexpr Bitboard  Rank7   = Us == WHITE ? Rank7BB : Rank2BB;
    constexpr Bitboard  Rank3   = Us == WHITE ? Rank3BB : Rank6BB;

    Bitboard pawns         = board.pieces(PAWN, Us);
    Bitboard enemies       = board.pieces(Them);
    Bitboard empty         = ~board.pieces();
    Bitboard promoPawns    = pawns & Rank7;
    Bitboard nonPromoPawns = pawns & ~Rank7;

    // Captures (including capture-promotions and en passant).
    if constexpr (GT == CAPTURES || GT == ALL_MOVES)
    {
        // Non-promo captures.
        Bitboard leftCap  = shift<UpLeft>(nonPromoPawns) & enemies & target;
        Bitboard rightCap = shift<UpRight>(nonPromoPawns) & enemies & target;

        while (leftCap)
        {
            Square to = pop_lsb(leftCap);
            *list++   = make_move(Square(to - UpLeft), to);
        }
        while (rightCap)
        {
            Square to = pop_lsb(rightCap);
            *list++   = make_move(Square(to - UpRight), to);
        }

        // Capture-promotions.
        Bitboard promoLeft  = shift<UpLeft>(promoPawns) & enemies;
        Bitboard promoRight = shift<UpRight>(promoPawns) & enemies;
        while (promoLeft)
        {
            Square to = pop_lsb(promoLeft);
            list      = make_promotions<GT>(list, Square(to - UpLeft), to);
        }
        while (promoRight)
        {
            Square to = pop_lsb(promoRight);
            list      = make_promotions<GT>(list, Square(to - UpRight), to);
        }

        // En passant — only generate if the ep square or the captured pawn is
        // inside the evasion target mask. This matters specifically during check
        // evasion: an en passant capture is legal as a check response only if it
        // either captures the checking pawn itself, or lands on a square that
        // blocks the check - both cases are covered by checking capsq/ep against
        // `target`, since `target` during evasions is the block-or-capture mask.
        Square ep = board.ep_square();
        if (ep != SQ_NONE)
        {
            Square capsq = Us == WHITE ? Square(ep - 8) : Square(ep + 8);
            if ((square_bb(ep) | square_bb(capsq)) & target)
            {
                Bitboard epPawns = pawn_attacks(Them, ep) & nonPromoPawns;
                while (epPawns)
                {
                    Square from = pop_lsb(epPawns);
                    *list++     = make_move(from, ep, MT_EN_PASSANT);
                }
            }
        }
    }

    // Quiet pawn moves: single/double pushes and non-capture promotions.
    if constexpr (GT == QUIETS || GT == ALL_MOVES)
    {
        Bitboard singlePush = shift<Up>(nonPromoPawns) & empty;
        Bitboard doublePush = shift<Up>(singlePush & Rank3) & empty & target;
        singlePush &= target;

        while (singlePush)
        {
            Square to = pop_lsb(singlePush);
            *list++   = make_move(Square(to - Up), to);
        }
        while (doublePush)
        {
            Square to = pop_lsb(doublePush);
            *list++   = make_move(Square(to - Up - Up), to);
        }

        // Quiet promotions.
        Bitboard quietPromo = shift<Up>(promoPawns) & empty & target;
        while (quietPromo)
        {
            Square to = pop_lsb(quietPromo);
            list      = make_promotions<GT>(list, Square(to - Up), to);
        }
    }

    return list;
}

// Shared generator for knight/bishop/rook/queen (any piece type whose
// legal squares come directly from an attacks_bb() lookup with no special
// movement rules). Handles pins inline: if the moving piece is pinned
// (sits in blockers_for_king()), its destination squares are restricted
// to the line between the king and the pinning piece - a pinned piece can
// still move along the pin line (including capturing the pinner) but
// can't step off it without exposing the king.
template <PieceType Pt, GenType GT>
static FORCE_INLINE Move *generate_piece_moves(const Board &board,
    Move                                                   *list,
    Color                                                   us,
    Bitboard                                                target,
    Bitboard                                                occ)
{
    Bitboard pieces = board.pieces(Pt, us);
    Bitboard pinned = board.blockers_for_king(us);
    Square   ksq    = board.king_square(us);

    while (pieces)
    {
        Square from = pop_lsb(pieces);

        Bitboard atk = attacks_bb(Pt, from, occ) & target;

        if (pinned & square_bb(from))
            atk &= line_bb(from, ksq);

        while (atk)
        {
            Square to = pop_lsb(atk);
            *list++   = make_move(from, to);
        }
    }

    return list;
}

// King moves are validated for safety inline (unlike other piece types,
// which rely on later filtering) since it's cheap to check here: for each
// candidate destination, temporarily remove the king from occupancy
// (`newOcc`) so sliding-piece attacks correctly X-ray through the king's
// old square - otherwise a rook/bishop attacking the king from behind
// would appear blocked by the king itself, incorrectly allowing the king
// to "escape" along the same line it's still exposed to.
template <GenType GT>
static FORCE_INLINE Move *generate_king_moves(const Board &board,
    Move                                                  *list,
    Color                                                  us,
    Bitboard                                               target,
    Bitboard                                               occ)
{
    Square   from   = board.king_square(us);
    Bitboard newOcc = occ ^ square_bb(from);  // remove king for X-ray
    Bitboard atk    = king_attacks(from) & target & ~board.pieces(KING);

    while (atk)
    {
        Square to = pop_lsb(atk);
        if (!(board.attackers_to(to, newOcc) & board.pieces(~us)))
            *list++ = make_move(from, to);
    }
    return list;
}

// Castling has its own dedicated legality checks since it's a compound
// move (king + rook) with multiple conditions the generic king-move logic
// doesn't cover: the path between king and rook must be entirely clear,
// and every square the king passes through (including its start and end
// square) must be unattacked - "can't castle out of, through, or into
// check." Returns early with no moves if already in check (evasion
// generation handles that case separately) or if generating captures only,
// since castling is never a capture.
template <Color Us, GenType GT>
static FORCE_INLINE Move *generate_castling(const Board &board, Move *list, Bitboard occ)
{
    if constexpr (GT == CAPTURES)
        return list;

    if (board.in_check())
        return list;

    constexpr CastlingRights OO  = Us == WHITE ? WHITE_OO : BLACK_OO;
    constexpr CastlingRights OOO = Us == WHITE ? WHITE_OOO : BLACK_OOO;

    for (CastlingRights cr : { OO, OOO })
    {
        if (!board.can_castle(cr))
            continue;

        Square kfrom = board.king_square(Us);
        if (kfrom != CASTLING_DATA[cr].kingSrc)
            continue;

        Square    kto   = CASTLING_DATA[cr].kingDest;
        Square    rfrom = CASTLING_DATA[cr].rookSrc;
        Direction d     = (kto > kfrom) ? EAST : WEST;

        if (board.piece_on(rfrom) != makePiece(Us, ROOK))
            continue;
        if (between_bb(kfrom, rfrom) & occ)
            continue;

        // Remove the king from occupancy for the same X-ray reason as
        // generate_king_moves() above, then walk every square from the king's
        // start to its destination (inclusive) checking each for enemy attacks -
        // this covers "in check," "castling through check," and "castling into
        // check" all in a single loop.
        Bitboard noKingOcc = occ ^ square_bb(kfrom);
        bool     safe      = true;
        for (Square s = kfrom;; s = Square(s + d))
        {
            if ((pawn_attacks(Us, s) & board.pieces(PAWN, ~Us))
                | (knight_attacks(s) & board.pieces(KNIGHT, ~Us))
                | (bishop_attacks(s, noKingOcc) & board.pieces(BISHOP, QUEEN, ~Us))
                | (rook_attacks(s, noKingOcc) & board.pieces(ROOK, QUEEN, ~Us))
                | (king_attacks(s) & board.pieces(KING, ~Us)))
            {
                safe = false;
                break;
            }
            if (s == kto)
                break;
        }

        if (safe)
            *list++ = make_move(kfrom, kto, MT_CASTLING);
    }

    return list;
}

// Evasion generation (in-check positions)
//
// 1. Always generate king escapes.
// 2. If double check, only king moves are legal — return early.
// 3. If single check, generate all moves that block or capture the checker.

// Only called when the side to move is in check (see the dispatch in
// generate_all_for_color() below). Always Templated on ALL_MOVES rather
// than a passed-in GT, since check evasion needs the complete legal
// response set regardless of what generation category the caller
// originally asked for - a captures-only search still needs to see
// blocking moves if that's the only way to escape check.
template <Color Us>
static FORCE_INLINE Move *generate_evasions(const Board &board, Move *list, Bitboard occ)
{
    Square   ksq      = board.king_square(Us);
    Bitboard checkers = board.checkers();

    // King escape squares: same X-ray-safe attacker check as
    // generate_king_moves(), applied here specifically for the in-check case.
    Bitboard kingTargets = king_attacks(ksq) & ~board.pieces(Us);
    Bitboard temp        = kingTargets;
    while (temp)
    {
        Square   to           = pop_lsb(temp);
        Bitboard afterKingOcc = (occ ^ square_bb(ksq)) | square_bb(to);
        if (!(board.attackers_to(to, afterKingOcc) & board.pieces(~Us)))
            *list++ = make_move(ksq, to);
    }

    // Double check: no single move can block or capture two checkers at once,
    // so only king moves (already generated above) can possibly be legal.
    if (more_than_one(checkers))
        return list;

    // Single checker: block or capture.
    // blockTarget is every square that would resolve the check: the checking
    // piece's own square (to capture it) unioned with the squares strictly
    // between the king and checker (to block a sliding check). For a
    // knight/pawn checker, between_bb() is empty since they can't be blocked,
    // so blockTarget naturally reduces to just the checker's square.
    Square   checker     = lsb_sq(checkers);
    Bitboard blockTarget = between_bb(ksq, checker) | checkers;

    list = generate_pawn_moves<Us, ALL_MOVES>(board, list, blockTarget);
    list = generate_piece_moves<KNIGHT, ALL_MOVES>(board, list, Us, blockTarget, occ);
    list = generate_piece_moves<BISHOP, ALL_MOVES>(board, list, Us, blockTarget, occ);
    list = generate_piece_moves<ROOK, ALL_MOVES>(board, list, Us, blockTarget, occ);
    list = generate_piece_moves<QUEEN, ALL_MOVES>(board, list, Us, blockTarget, occ);

    return list;
}

// Top-level per-color dispatcher: routes to evasion generation if in
// check, otherwise computes the appropriate `target` mask for the
// requested GenType and calls each piece-type's generator in turn.
template <Color Us, GenType GT>
static FORCE_INLINE Move *generate_all_for_color(const Board &board, Move *list)
{
    Bitboard usBB = board.pieces(Us);
    Bitboard occ  = board.pieces();

    if (board.checkers())
        return generate_evasions<Us>(board, list, occ);

    // target = the set of squares any generated move is allowed to land on:
    //   CAPTURES - enemy pieces only (excluding their king, which can never
    //              legally be captured)
    //   QUIETS   - empty squares only
    //   ALL_MOVES - anywhere not occupied by our own pieces or their king
    Bitboard target;
    if constexpr (GT == CAPTURES)
        target = board.pieces(~Us) & ~board.pieces(KING);
    else if constexpr (GT == QUIETS)
        target = ~occ;
    else
        target = ~usBB & ~board.pieces(KING);

    // Pawns need their own target for the CAPTURES case specifically: a pawn
    // capture target should be "any enemy piece," matching generate_pawn_moves'
    // internal capture logic, whereas the generic `target` computed above
    // already excludes the enemy king - redundant for pawns since a pawn
    // capturing the king can't happen in a legal position anyway, but this
    // keeps the semantics explicit rather than relying on that never mattering.
    Bitboard pawnTarget = (GT == CAPTURES) ? board.pieces(~Us) : target;

    list = generate_pawn_moves<Us, GT>(board, list, pawnTarget);
    list = generate_piece_moves<KNIGHT, GT>(board, list, Us, target, occ);
    list = generate_piece_moves<BISHOP, GT>(board, list, Us, target, occ);
    list = generate_piece_moves<ROOK, GT>(board, list, Us, target, occ);
    list = generate_piece_moves<QUEEN, GT>(board, list, Us, target, occ);
    list = generate_king_moves<GT>(board, list, Us, ~usBB, occ);

    if constexpr (GT == QUIETS || GT == ALL_MOVES)
        list = generate_castling<Us, GT>(board, list, occ);

    return list;
}

template <GenType GT> Move *generate(const Board &board, Move *list)
{
    return board.side_to_move() == WHITE ? generate_all_for_color<WHITE, GT>(board, list)
                                         : generate_all_for_color<BLACK, GT>(board, list);
}

// Explicit instantiation definitions matching the `extern template`
// declarations in movegen.h - this is where the template code actually
// gets compiled, once per GenType, in this translation unit only.
template Move *generate<ALL_MOVES>(const Board &, Move *);
template Move *generate<CAPTURES>(const Board &, Move *);
template Move *generate<QUIETS>(const Board &, Move *);

// Generates pseudo-legal moves into a stack buffer, then filters through
// is_legal() one at a time - simple and correct, but pays the cost of
// generating and validating every move regardless of category. Fine for
// perft/UCI/datagen; search uses the streaming generate<GT>() + selective
// legality checking instead to avoid this overhead on the hot path.
MoveList generate_legal(Board &board)
{
    MoveList legal;

    Move  pseudoBuf[MAX_MOVES];
    Move *pseudoEnd = generate<ALL_MOVES>(board, pseudoBuf);

    for (Move *it = pseudoBuf; it != pseudoEnd; ++it)
    {
        if (board.is_legal(*it))
            legal.push(*it);
    }

    return legal;
}

// Convenience wrapper - not optimized (still builds the full MoveList
// internally), but simple and correct; fine for the non-hot-path callers
// that use it (e.g. detecting checkmate/stalemate by legal move count).
int count_legal(Board &board)
{
    return generate_legal(board).size();
}

}  // namespace Catalyst