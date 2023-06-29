/* File containing code for initializing and lookup of precalculated attack
 * tables */

#include "attack.h"

// For pawns, we index the attack table by [side to move] and [origin square].
// We get a bitboard back, representing the squares being attacked by pawn of
// color [side to move] located on [origin square]
bb_t pawn_attacks[2][SQUARE_NO];

/* Leaping pieces */
bb_t knight_attacks[SQUARE_NO];
bb_t king_attacks[SQUARE_NO];

/* Sliding pieces */
// TODO: Magics, PEXT
bb_t bishop_occupancies[SQUARE_NO];
bb_t rook_occupancies[SQUARE_NO];
bb_t bishop_attacks[SQUARE_NO];
bb_t rook_attacks[SQUARE_NO];

void init_pawn_attacks() {
    // Reset current boards
    for (square_t sq = A1; sq <= H8; ++sq) {
        pawn_attacks[BLACK][sq] = 0ULL;
        pawn_attacks[WHITE][sq] = 0ULL;
    }
    // TODO: set pawn attacks
}

void init_leap_attacks() {
    // Reset current attack boards
    for (square_t sq = A1; sq <= H8; ++sq) {
        knight_attacks[sq] = 0ULL;
        king_attacks[sq] = 0ULL;
    }

    // Compute attacks for the king for each origin square
    bb_t bb;
    for (square_t sq = A1; sq <= H8; ++sq) {
        bb = SQ_TO_BB(sq);
        king_attacks[sq] |= n_shift(bb);
        king_attacks[sq] |= ne_shift(bb);
        king_attacks[sq] |= e_shift(bb);
        king_attacks[sq] |= se_shift(bb);
        king_attacks[sq] |= s_shift(bb);
        king_attacks[sq] |= sw_shift(bb);
        king_attacks[sq] |= w_shift(bb);
        king_attacks[sq] |= nw_shift(bb);
    }

    // Compute attacks for the knight for each origin square
    for (square_t sq = A1; sq <= H8; ++sq) {
        bb = SQ_TO_BB(sq);
        knight_attacks[sq] |= ne_shift(n_shift(bb));
        knight_attacks[sq] |= ne_shift(e_shift(bb));
        knight_attacks[sq] |= se_shift(e_shift(bb));
        knight_attacks[sq] |= se_shift(s_shift(bb));
        knight_attacks[sq] |= sw_shift(s_shift(bb));
        knight_attacks[sq] |= sw_shift(w_shift(bb));
        knight_attacks[sq] |= nw_shift(w_shift(bb));
        knight_attacks[sq] |= nw_shift(n_shift(bb));
    }
}

// Set relevant occupancy bits for a bishop on each origin square
void init_bishop_occupancies() {
    for (square_t sq = A1; sq <= H8; ++sq) {
        bb_t occupancies = 0ULL;

        // Bitboard representing the destination square
        bb_t start = SQ_TO_BB(sq);
        bb_t dest = start;

        // NORTH_EAST
        while ((dest = ne_shift(dest))) {
            occupancies |= dest;
        }

        // SOUTH_EAST
        dest = start;
        while ((dest = se_shift(dest))) {
            occupancies |= dest;
        }

        // SOUTH_WEST
        dest = start;
        while ((dest = sw_shift(dest))) {
            occupancies |= dest;
        }

        // NORTH_WEST
        dest = start;
        while ((dest = nw_shift(dest))) {
            occupancies |= dest;
        }

        // For occupancies, we don't care the squares on the border of the board
        bishop_occupancies[sq] = occupancies & ~BORDER_SQ;
    }
}

// Set relevant occupancy bits for a rook on each origin square
void init_rook_occupancies() {
    for (square_t sq = A1; sq <= H8; ++sq) {
        bb_t occupancies = 0ULL;

        // Bitboard representing the destination square
        bb_t start = SQ_TO_BB(sq);
        bb_t dest = start;

        // NORTH
        while ((dest = n_shift(dest))) {
            occupancies |= dest;
        }

        // EAST
        dest = start;
        while ((dest = e_shift(dest))) {
            occupancies |= dest;
        }

        // SOUTH
        dest = start;
        while ((dest = s_shift(dest))) {
            occupancies |= dest;
        }

        // WEST
        dest = start;
        while ((dest = w_shift(dest))) {
            occupancies |= dest;
        }

        // For occupancies, we don't care the squares on the border of the board
        rook_occupancies[sq] = occupancies & ~BORDER_SQ;
    }
}

bb_t gen_bishop_attacks(square_t sq, bb_t blockers) {

    bb_t attacks = 0ULL;

    // Bitboard representing the destination square
    bb_t start = SQ_TO_BB(sq);
    bb_t dest = start;

    // NORTH_EAST
    while ((dest = ne_shift(dest))) {
        attacks |= dest;
        if (dest & blockers) break;
    }

    // SOUTH_EAST
    dest = start;
    while ((dest = se_shift(dest))) {
        attacks |= dest;
        if (dest & blockers) break;
    }

    // SOUTH_WEST
    dest = start;
    while ((dest = sw_shift(dest))) {
        attacks |= dest;
        if (dest & blockers) break;
    }

    // NORTH_WEST
    dest = start;
    while ((dest = nw_shift(dest))) {
        attacks |= dest;
        if (dest & blockers) break;
    }

    return attacks;
}

bb_t gen_rook_attacks(square_t sq, bb_t blockers) {

    bb_t attacks = 0ULL;

    // Bitboard representing the destination square
    bb_t start = SQ_TO_BB(sq);
    bb_t dest = start;

    // NORTH
    while ((dest = n_shift(dest))) {
        attacks |= dest;
        if (dest & blockers) break;
    }

    // EAST
    dest = start;
    while ((dest = e_shift(dest))) {
        attacks |= dest;
        if (dest & blockers) break;
    }

    // SOUTH
    dest = start;
    while ((dest = s_shift(dest))) {
        attacks |= dest;
        if (dest & blockers) break;
    }

    // WEST
    dest = start;
    while ((dest = w_shift(dest))) {
        attacks |= dest;
        if (dest & blockers) break;
    }

    return attacks;
}


void init_slider_attacks() {
    for (square_t sq = A1; sq <= H8; ++sq) {
        bishop_attacks[sq] = 0ULL;
        rook_attacks[sq] = 0ULL;
    }
}
