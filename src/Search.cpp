#include "Board.h"
#include "MoveGenerator.h"

#define MATE INT32_MAX;

// 10 MiB PV table size
static const int PV_SIZE = 0xA00000;

static void checkUp() {
    // TODO: Check if time is up
    // or interrupted by GUI
}

void init_PVtable(pvtable_t *table) {
    table->no_entries = PV_SIZE / sizeof(pventry_t) - 1;
    if (table->pvtable != nullptr) {
        delete[] table->pvtable;
    }
    // allocate the space for the table
    table->pvtable = new pventry_t[table->no_entries]();
}

// Detect whether the current board state has been seen before on the board stack

// TODO: Can use the fiftyMoveRule counter to optimize this
// since we don't need to search the *entire* history
static bool isRepetition(Board& b) {
    std::vector<undo_t>& hist = b.boardHistory;
    int i = 0, n = hist.size();
    for (; i < n - 1; i++) {
        if (hist[i].posKey == b.posKey) {
            return true;
        }
    }
    return false;
}

void storePvMove(const Board& b, const Move move) {

    // we use the posKey as a hash into our table!
    int key = b.posKey % b.PVtable.no_entries;
    b.PVtable.pvtable[key].move = move;
    b.PVtable.pvtable[key].posKey = b.posKey;
}

Move checkPvTable(Board& b) {
    int key = b.posKey % b.PVtable.no_entries;
    if (b.PVtable.pvtable[key].posKey == b.posKey) {
        return b.PVtable.pvtable[key].move;
    }
    return Move(0, 0); // Null (zero) move
}

bool moveExists(Board& b, Move m) {
    std::vector<Move> moves = generateMoves(b);
    for (Move& move : moves) {
        // Check if legal move
        if (!b.makeMove(move)) {
            continue;
        }
        b.undoMove(move);
        // Check if move is the move m we're looking for
        if (move == m) {
            return true;
        }
    }
    // m wasn't found, hence m isn't a valid move
    return false;
}

int getPV(Board& b, const int depth) {
    Move move = checkPvTable(b);
    int currDepth = 0;

    while (move != NULLMV && currDepth < depth) {
        if (moveExists(b, move)) {
            b.makeMove(move);
            b.pv.push_back(move);
            currDepth++;
        } else {
            break;
        }
        move = checkPvTable(b);
    }

    // Ply starts at 0 when the current position is searched
    while (b.ply > 0) {
        b.undoLast();
    }

    return currDepth;
}

/* Evaluation */

// Piece-square tables

const int PawnTable[64] = {
     0,   0,   0,   0,   0,   0,   0,   0,
    10,  10,   0, -10, -10,   0,  10,  10,
     5,   0,   0,   5,   5,   0,   0,   5,
     0,   0,  10,  20,  20,  10,   0,   0,
     5,   5,   5,  10,  10,   5,   5,   5,
    10,  10,  10,  20,  20,  10,  10,  10,
    20,  20,  20,  30,  30,  20,  20,  20,
     0,   0,   0,   0,   0,   0,   0,   0
};

const int KnightTable[64] = {
     0, -10,   0,   0,   0,   0, -10,   0,
     0,   0,   0,   5,   5,   0,   0,   0,
     0,   0,  10,  10,  10,  10,   0,   0,
     0,   0,  10,  20,  20,  10,   5,   0,
     5,  10,  15,  20,  20,  15,  10,   5,
     5,  10,  10,  20,  20,  10,  10,   5,
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0
};

const int BishopTable[64] = {
     0,   0, -10,   0,   0, -10,   0,   0,
     0,   0,   0,  10,  10,   0,   0,   0,
     0,   0,  10,  15,  15,  10,   0,   0,
     0,  10,  15,  20,  20,  15,  10,   0,
     0,  10,  15,  20,  20,  15,  10,   0,
     0,   0,  10,  15,  15,  10,   0,   0,
     0,   0,   0,  10,  10,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0
};

const int RookTable[64] = {
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   0,   5,  10,  10,   5,   0,   0,
     0,   0,   5,  10,  10,   5,   0,   0,
    25,  25,  25,  25,  25,  25,  25,  25,
     0,   0,   5,  10,  10,   5,   0,   0
};

// Table for mirroring the piece square table
const int Mirror64[64] = {
    56, 57, 58, 59, 60, 61, 62, 63,
    48, 49, 50, 51, 52, 53, 54, 55,
    40, 41, 42, 43, 44, 45, 46, 47,
    32, 33, 34, 35, 36, 37, 38, 39,
    24, 25, 26, 27, 28, 29, 30, 31,
    16, 17, 18, 19, 20, 21, 22, 23,
    8,  9,  10, 11, 12, 13, 14, 15,
    0,  1,  2,  3,  4,  5,  6,  7
};

// Evaluates the position from the side's POV
int evaluate(Board& b) {
    using namespace Piece;
    //          White           Black
    int score = b.material[1] - b.material[0];
    int delta = 0;
    piece* board = b.board;
    for (square_t s = A1; s <= H8; ++s) {
        piece p = board[s];
        if (p != None) {

            switch (PieceType(p)) {
                case Pawn: {
                    if (IsColour(p, White)) {
                        delta = PawnTable[s];
                    } else {
                        delta = -PawnTable[Mirror64[s]];
                    }
                    break;
                }
                case Knight: {
                    if (IsColour(p, White)) {
                        delta = KnightTable[s];
                    } else {
                        delta = -KnightTable[Mirror64[s]];
                    }
                    break;
                }
                case Rook: {
                    if (IsColour(p, White)) {
                        delta = RookTable[s];
                    } else {
                        delta = -RookTable[Mirror64[s]];
                    }
                    break;
                }
                case Bishop: {
                    if (IsColour(p, White)) {
                        delta = BishopTable[s];
                    } else {
                        delta = -BishopTable[Mirror64[s]];
                    }
                    break;
                }
                // TODO: Tune this
                case Queen: {
                    if (IsColour(p, White)) {
                        delta = (BishopTable[s] + RookTable[s]) / 2;
                    } else {
                        delta = -(BishopTable[Mirror64[s]] + RookTable[Mirror64[s]]) / 2;
                    }
                    break;
                }
                default:
                    break;
            }
            score += delta;
    }
    return score;
}

static void clearForSearch(searchinfo_t *info) {
    memset(info, 0, sizeof(searchinfo_t));
}

static int quiescenceSearch(Board& b, searchinfo_t, int alpha, int beta) {
    // TODO: Implement
    return 0;
}

static int alphaBeta(Board& b, searchinfo_t *info, int alpha, int beta, int depth, bool canDoNull) {

    info->nodes++;

    if (depth == 0) {
        return evaluate(b);
    }

    // Check if position is a draw
    if (isRepetition(b) || b.fiftyMoveCounter >= 100) {
        return 0;
    }

    // Generate pseudolegal moves
    std::vector<Move> moves = generateMoves(b);
    int moveNum = 0, legal = 0;
    int oldAlpha = alpha;
    Move bestMv = NULLMV;
    int score = -INT32_MAX;

    for (; moveNum < moves.size(); ++moveNum) {
        if (!b.makeMove(moves[moveNum])) continue;
        legal++;
        score = -alphaBeta(b, info, -beta, -alpha, depth - 1, true);
        b.undoMove(moves[moveNum]);

        if (score > alpha) {
            if (score >= beta) {
                return beta;
            }
            alpha = score;
            bestMv = moves[moveNum];
        }
    }

    // Detect mate / stalemate
    if (!legal) {
        if (b.SquareAttacked(b.kingSquare[b.turn == Piece::White], OPPONENT(b.turn))) {
            return -MATE + b.ply;
        } else {
            return 0; // stalemate
        }
    }

    if (alpha != oldAlpha) {
        storePvMove(b, bestMv);
    }

    return alpha;
}

// Searches the position defined by Board b
void search(Board& b, searchinfo_t *info) {
    Move bestMv = NULLMV;
    int bestScore = -INT32_MAX; // - Infinity
    int currDepth = 0;
    int pvMoves = 0;
    int pvNum = 0;
    clearForSearch(info);

    // Iterative Deepening
    for (currDepth = 0; currDepth < info->depth; ++currDepth) {
        bestScore = alphaBeta(b, info, -INT32_MAX, +INT32_MAX, currDepth, true);
        pvMoves = getPV(b, currDepth);
        bestMv = b.pv[0];
        printf("Depth:%d score:%d move:%s nodes:%lld ",
                currDepth, bestScore, bestMv.toString().c_str(), info->nodes);

        // TODO:
        //pvMoves = getPV(b, currDepth);
        printf("pv");
        for (pvNum = 0; pvNum < pvMoves; ++pvNum) {
            printf(" %s", b.pv[pvNum].toString().c_str());
        }
        printf("\n");
    }

}
