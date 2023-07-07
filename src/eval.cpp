/* Evaluation */
#include "eval.h"

namespace {

// Piece-square tables
// We use midgame and endgame tables between which we interpolate
constexpr int pawn_table_mg[] = {
     0,   0,   0,   0,   0,   0,   0,   0,
    10,  10,   0, -10, -10,   0,  10,  10,
     5,   0,   0,   5,   5,   0,   0,   5,
     0,   0,  10,  20,  20,  10,   0,   0,
     5,   5,   5,  10,  10,   5,   5,   5,
    10,  10,  10,  20,  20,  10,  10,  10,
    20,  20,  20,  30,  30,  20,  20,  20,
     0,   0,   0,   0,   0,   0,   0,   0
};

constexpr int pawn_table_eg[] = {
     0,   0,   0,   0,   0,   0,   0,   0,
     8,   8,   5,   4,   4,   5,   8,   8,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     2,   0,   0,  -1,  -1,  10,   0,   2,
    25,  10,   5,   5,   5,   5,  10,  25,
    55,  50,  50,  45,  45,  50,  50,  55,
   125, 120, 120, 110, 110, 120, 120, 125,
     0,   0,   0,   0,   0,   0,   0,   0
};

constexpr int knight_table_mg[] = {
    -5, -10,   0,   0,   0,   0, -10,  -5,
     0,   0,   0,   5,   5,   0,   0,   0,
     0,   0,  10,  10,  10,  10,   0,   0,
     0,   0,  10,  20,  20,  10,   5,   0,
     5,  10,  15,  20,  20,  15,  10,   5,
     5,  10,  10,  20,  20,  10,  10,   5,
     0,   0,   5,  10,  10,   5,   0,   0,
    -5,   0,   0,   0,   0,   0,   0,  -5
};

constexpr int knight_table_eg[] = {
     -35, -10,  -5,  -5,  -5,  -5, -10,  -35,
     -10,   0,   5,  10,  10,   5,   0,  -10,
     -10,  10,  10,  10,  10,  10,  10,  -10,
     -10,  10,  10,  20,  20,  10,  10,  -10,
     -10,  10,  15,  20,  20,  15,  10,  -10,
     -10,  10,  10,  20,  20,  10,  10,  -10,
     -10,   0,   5,  10,  10,   5,   0,  -10,
     -35, -10,  -5,  -5,  -5,  -5, -10,  -35,
};

constexpr int bishop_table_mg[] = {
     0,   0, -10,   0,   0, -10,   0,   0,
     0,   0,   0,  10,  10,   0,   0,   0,
     0,   0,  10,  15,  15,  10,   0,   0,
     0,  10,  15,  20,  20,  15,  10,   0,
     0,  10,  15,  20,  20,  15,  10,   0,
     0,   0,  10,  15,  15,  10,   0,   0,
     0,   0,   0,  10,  10,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0
};

constexpr int bishop_table_eg[] = {
   -20, -10, -10,   0,   0, -10, -10, -20,
     0,   0,   0,  10,  10,   0,   0,   0,
     0,   0,  10,  15,  15,  10,   0,   0,
     0,  10,  15,  20,  20,  15,  10,   0,
     0,  10,  15,  20,  20,  15,  10,   0,
     0,   0,  10,  15,  15,  10,   0,   0,
     0,   0,   0,  10,  10,   0,   0,   0,
   -20, -10, -10,   0,   0, -10, -10, -20,
};

constexpr int rook_table_mg[] = {
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   0,   5,  10,  10,   5,   0,   0,
    25,  25,  25,  25,  25,  25,  25,  25,
     0,   0,   5,  10,  10,   5,   0,   0
};

constexpr int rook_table_eg[] = {
     0,   0,   5,   5,   5,   5,   0,   0,
     0,   0,   5,   5,   5,   5,   0,   0,
     0,   0,   5,   5,   5,   5,   0,   0,
     0,   0,   5,   5,   5,   5,   0,   0,
     0,   0,   5,   5,   5,   5,   0,   0,
     0,   0,   5,   5,   5,   5,   0,   0,
    15,  15,  15,  15,  15,  15,  15,  15,
     0,   0,   5,  10,  10,   5,   0,   0
};


constexpr int king_table_mg[] = {
   -50, -10,   0,   0,   0,   0, -10, -50,
   -10,   0,  10,  10,  10,  10,   0, -10,
     0,  10,  20,  20,  20,  20,  10,   0,
     0,  10,  20,  40,  40,  20,  10,   0,
     0,  10,  20,  40,  40,  20,  10,   0,
     0,  10,  20,  20,  20,  20,  10,   0,
   -10,   0,  10,  10,  10,  10,   0, -10,
   -50, -10,   0,   0,   0,   0, -10, -50
};

constexpr int king_table_eg[] = {
     0,   5,   5, -10, -10,   0,  10,   5,
   -30, -30, -30, -30, -30, -30, -30, -30,
   -50, -50, -50, -50, -50, -50, -50, -50,
   -70, -70, -70, -70, -70, -70, -70, -70,
   -70, -70, -70, -70, -70, -70, -70, -70,
   -70, -70, -70, -70, -70, -70, -70, -70,
   -70, -70, -70, -70, -70, -70, -70, -70,
   -70, -70, -70, -70, -70, -70, -70, -70
};


} // namespace

// Evaluates the position from the side's POV
int evaluate(const board_t *board) {
    assert(check(board));

    int score = 0;

    int ps = CNT(board->bitboards[p]);
    int Ps = CNT(board->bitboards[P]);

    int ns = CNT(board->bitboards[n]);
    int Ns = CNT(board->bitboards[N]);

    int bs = CNT(board->bitboards[b]);
    int Bs = CNT(board->bitboards[B]);

    int rs = CNT(board->bitboards[r]);
    int Rs = CNT(board->bitboards[R]);

    int qs = CNT(board->bitboards[q]);
    int Qs = CNT(board->bitboards[Q]);

    score  = Ps + 3*Ns + 3*Bs + 5*Rs + 9*Qs;
    score -= ps + 3*ns + 3*bs + 5*rs + 9*qs;

    return board->turn ? score : -score;
}
