#ifndef EVAL_H_
#define EVAL_H_

#include <algorithm> // std::min, std::max

#include "types.h"
#include "board.h"

/**
 @brief This struct scores all relevant data for the evaluation of given board position
 */
typedef struct eval_t {
    // Game phase (0, 256)
    int phase = 0;
    // Middlegame score
    int middlegame = 0;
    // Endgame score
    int endgame = 0;
    // Tapered score
    int score = 0;

    /**
    @brief Calculates the current game phase and sets the corresponding field in eval

    Phase can range from 0 to 256 (no pieces except kings -> all pieces)
    The closer to the endgame we are, the more heavily endgame psqts
    are weighted. We use min(max(0, 1.5x - 64), 256) to scale between game phases

    @param board boards state to calculate the game phase for
    @param eval eval_t struct to store the game phase in
    */
    inline void set_phase(const board_t *board) {
        phase = CNT(board->bitboards[p] | board->bitboards[P]) << 2;
        phase += 6 * CNT(board->bitboards[k] | board->bitboards[K]);
        phase += 12 * CNT(board->bitboards[b] | board->bitboards[B]);
        phase += 18 * CNT(board->bitboards[r] | board->bitboards[R]);
        phase += 40 * CNT(board->bitboards[q] | board->bitboards[Q]);
        phase *= 3;
        phase -= 128;
        phase >>= 1;
        phase = std::min(std::max(0, phase), 256);
    }

    inline int get_tapered_score() {
        return score = (middlegame * phase + endgame * (256 - phase)) / 256;
    }

    inline void print() {
        std::cout << "Phase: " << phase \
                  << " Middlegame score: " << middlegame \
                  << " Endgame score: " << endgame \
                  << " Final score: " << score << std::endl;
    }
} eval_t;

/**
 @brief Static tapered evaluation of the current board state

 Uses: piece values, piece-square tables, passed pawns, isolated pawns,
 rook/queen on open/semi-open files, basic king safety, bishop pairs,

 @param board boards state to evaluate
 @param eval eval_t struct storing evaluation data, used for the 'eval' command
 among others
 */
int evaluate(const board_t *board, eval_t *eval);

#endif // EVAL_H_