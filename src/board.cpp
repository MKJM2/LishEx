#include "board.h"

#include <cstring> //std::memset
#include <sstream> //std::istringstream
#include <string>
#ifdef DEBUG
#include <vector>
#include <iomanip>
#include <fstream>
#endif

#include "bitboard.h"
#include "attack.h"
#include "uci.h"
#include "types.h"
#include "movegen.h"
#include "rng.h"
#include "threads.h"
#include "eval.h"
#include "see.h"

#ifdef DEBUG
size_t boards = 0;
std::vector<board_t> ref_boards(64); // TODO: C-style array
#endif

/*******************/
/* Zobrist hashing */
/*******************/

uint64_t piece_keys[PIECE_NO][SQUARE_NO];
uint64_t turn_key = 0ULL;
uint64_t castle_keys[16]; // == WK | WQ | BK | BQ + 1 = 0b1111 + 1
uint64_t ep_keys[SQUARE_NO];

void init_keys() {
    seed_rng();
    // For each piece type and square generate a random key
    for (piece_t p : pieces) {
        for (square_t sq = A1; sq <= H8; ++sq) {
            piece_keys[p][sq] = rand_uint64();
        }
    }

    // Generate hash key for White's side to play
    turn_key = rand_uint64();

    // Generate hash keys for castling rights
    for (int i = 0; i < 16; ++i) {
        castle_keys[i] = rand_uint64();
    }

    // Generate hash keys for en passant squares
    // (though we only need 16, having 64 keys makes the code cleaner)
    for (square_t sq = A1; sq <= H8; ++sq) {
        ep_keys[sq] = rand_uint64();
    }
}

/* Zeroes out the entire position */
void reset(board_t *board) {

    // Throws compiler warnings:
    // std::memset(board, 0, sizeof(board_t));

    // Clear board history
    // memset(board->history, 0, MAX_MOVES * sizeof(undo_t));
    for (int his_ply = 0; his_ply < MAX_MOVES; ++his_ply) {
        board->history[his_ply] = {
            NULLMV, 0, NO_SQ, 0, 0ULL, NO_PIECE
        };
    }

    // Reset the 8x8 board
    memset(board->pieces, 0, sizeof(board->pieces));

    // Reset all bitboards
    for (piece_t pc = NO_PIECE; pc < PIECE_NO; ++pc) {
        board->bitboards[pc] = 0ULL;
    }
    board->sides_pieces[BLACK] = board->sides_pieces[WHITE] = 0ULL;

    // Reset the turn
    board->turn = BOTH;

    // Reset the en passant square
    board->ep_square = NO_SQ;

    // Reset the plies
    board->ply = 0;
    board->history_ply = 0;

    // Reset castling rights
    board->castle_rights = 0;

    // Reset 50rule counter
    board->fifty_move = 0;

    // Clear Zobrist board key
    board->key = 0ULL;

    // Reset king squares
    // board->king_square[WHITE] = board->king_square[BLACK] = NO_SQ;
}

// TODO: Parsing current board position to a FEN string

/* Initializes the board from given FEN string */
void setup(board_t *board, const std::string& fen) {
    // Split the input FEN into 6 parts
    // - position
    // - side to move
    // - castling rights
    // - en passant square
    // - halfmove clock
    // - fullmove clock
    // example: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
    std::string fen_parts[6];
    int idx = 0;
    size_t i = 0;
    for (; i < fen.size(); ++i) {
        if (fen[i] == ' ') {
            ++idx;
        } else {
            fen_parts[idx] += fen[i];
        }
    }

    // Make sure the board is reset
    reset(board);

    // Fill the bitboards with pieces from FEN
    int sq = A8;
    char c;
    for (i = 0; i < fen_parts[0].size(); ++i) {
        c = fen_parts[0][i];
        if (c == '/') {
            sq -= 2 * NORTH;
        } else if (isdigit(c)) {
            sq += (int)c - '0';
        } else {
            piece_t piece = char_to_piece[c];
            SETBIT(board->bitboards[piece], sq);
            SETBIT(board->sides_pieces[piece_color(piece)], sq);
            board->pieces[sq] = piece;
            ++sq;
        }
    }

    // Set the king squares
    assert(CNT(board->bitboards[K]) == CNT(board->bitboards[k]) == 1);
    assert(king_square_bb(board, WHITE) == board->bitboards[K]);
    assert(king_square_bb(board, WHITE) == board->bitboards[K]);

    // Set the side to move
    board->turn = (fen_parts[1] == "w") ? WHITE : BLACK;

    // Set the castling rights
    board->castle_rights = 0;
    if (fen_parts[2] != "-") {
        if (fen_parts[2].find('K') != std::string::npos) {
            board->castle_rights |= WK;
        }
        if (fen_parts[2].find('Q') != std::string::npos) {
            board->castle_rights |= WQ;
        }
        if (fen_parts[2].find('k') != std::string::npos) {
            board->castle_rights |= BK;
        }
        if (fen_parts[2].find('q') != std::string::npos) {
            board->castle_rights |= BQ;
        }
    }

    // Set the en passant square
    if (fen_parts[3] != "-") {
        int file = fen_parts[3][0] - 'a';
        int rank = fen_parts[3][1] - '1';
        board->ep_square = rank * 8 + file;
    } else {
        board->ep_square = NO_SQ;
    }

    // TODO: Set the halfmove clock since last capture/pawn push
    // Currently, we're using the ply as the search depth ply
    // if (fen_parts[4].size()) {
        // board->ply = stoi(fen_parts[4]);
    // }
    board->ply = 0;

    // TODO: Set the fullmove clock (currently not being used!)

    // Generate the starting position key for this FEN
    board->key = generate_pos_key(board);
}
/**
 @brief Returns a FEN representation of the current board
 @param board current position
 @return a FEN string representing current board state
*/
std::string to_fen(const board_t *board) {
    std::string fen;

    // Pieces
    for (int rank = RANK_8; rank >= RANK_1; --rank) {
        int empty_sqs = 0;
        for (int file = A_FILE; file <= H_FILE; ++file) {
            square_t sq = rank * RANK_NO + file;
            piece_t pce = board->pieces[sq];

            if (pce == NO_PIECE) {
                ++empty_sqs;
            } else {
                if (empty_sqs > 0) {
                    fen += std::to_string(empty_sqs);
                    empty_sqs = 0;
                }
                fen += piece_to_ascii[pce];
            }
        }

        if (empty_sqs > 0) {
            fen += std::to_string(empty_sqs);
        }

        if (rank > 0) {
            fen += '/';
        }
    }

    // Side to move
    fen += (board->turn) ? " w " : " b ";

    // Castling permissions
    fen += castling_rights_to_str(board->castle_rights);
    fen += ' ';

    // En passant square
    if (board->ep_square != NO_SQ) {
        fen += square_to_str(board->ep_square);
    } else {
        fen += '-';
    }

    // Halfmove clock
    fen += ' ';
    fen += std::to_string(board->ply);
    fen += ' ';

    // TODO: Fullmove clock

    return fen;
}

/**
 @brief Prints the board to stdout
 @param board board to print
 @param verbose whether to include additional information, like castling rights
        (default True)
*/
void print(const board_t *board, bool verbose) {
    // TODO: Overload the << operator for board printing
    // Print files on top of the board
    std::cout << "    a b c d e f g h" << std::endl;
    std::cout << "  ╔═════════════════╗ " << std::endl;
    // Loop over all ranks
    for (int rank = RANK_8; rank >= RANK_1; --rank) {
        std::cout << rank + 1 << " ║ ";
        // Loop over all files
        for (int file = A_FILE; file <= H_FILE; ++file) {
            // Current square to print
            square_t sq = rank * RANK_NO + file;

            piece_t pce = board->pieces[sq];
            // Print the piece
            //std::cout << piece_to_ascii[pce] << (file == H_FILE ? "" : " ");
            std::cout << piece_to_ascii[pce] << " ";
        }
        // Print rank number to the right of current rank
        std::cout << "║ " << rank + 1 << std::endl;
    }
    std::cout << "  ╚═════════════════╝ " << std::endl;
    // Print files underneath the board
    std::cout << "    a b c d e f g h" << std::endl << std::endl;

    if (!verbose)
        return;

    std::cout << "KEY: " << std::hex << std::showbase \
              << board->key << std::dec << std::endl;
    std::cout << "FEN: " << to_fen(board) << std::endl;
    /*
    // Print additional information, like castle permissions, side to move etc.
    std::cout << "Side to play: " \
              << ((board->turn) ? "White" : "Black") << std::endl;
    std::cout << "En Passant square: "
              << ((board->ep_square == NO_SQ) ? "null"
                                              : square_to_str(board->ep_square))
              << std::endl;
    std::cout << "Castle permissions: "
              << castling_rights_to_str(board->castle_rights) << std::endl;

    std::cout << "Zobrist hash key: " << std::hex \
              << board->key << std::dec << std::endl;

    std::cout << "King squares: White at " \
              << square_to_str(king_square(board, WHITE)) << ", Black at " \
              << square_to_str(king_square(board, BLACK)) << std::endl;
    */
}

/* Generates the position key from scratch for the current position */
uint64_t generate_pos_key(const board_t *board) {
    uint64_t key = 0ULL;

    // Hash in all the pieces on the board
    /*
    for (piece_t p : pieces) {
        for (square_t sq = A1; sq <= H8; ++sq) {
            if (GETBIT(board->bitboards[p], sq)) {
                key ^= piece_keys[p][sq];
            }
        }
    }
    */
    for (square_t sq = A1; sq <= H8; ++sq) {
        key ^= piece_keys[board->pieces[sq]][sq];
    }

    // Hash in the side playing
    if (board->turn == WHITE) {
        key ^= turn_key;
    }

    // Hash in castling rights
    key ^= castle_keys[board->castle_rights];

    // Hash in the en passant square (if set)
    if (board->ep_square != NO_SQ) {
        key ^= ep_keys[board->ep_square];
    }

    return key;
}


bool is_repetition(const board_t *board) {
    // We'll search the history backwards starting from last possible
    // repetition, which is 2 halfmoves ago
    int i = 2;
    // Since captures & pawn pushes are irreversible,
    // we don't have to check the entire history for repetitions
    // but only fifty move counter moves back
    for (; i <= board->fifty_move; i += 2) {
        if (board->key == board->history[board->history_ply - i].key) {
            return true;
        }
    }
    return false;
}

void test(board_t *board) {

}

void test_see(board_t *board) {
    setup(board, "1k1r4/1pp4p/p7/4p3/8/P5P1/1PP4P/2K1R3 w - -");
    print(board);
    std::cout << see(board, Move(E1, E5, CAPTURE)) << std::endl;

    setup(board, "1k1r3q/1ppn3p/p4b2/4p3/8/P2N2P1/1PP1R1BP/2K1Q3 w - -");
    print(board);
    std::cout << see(board, Move(D3, E5, CAPTURE)) << std::endl;

    setup(board, "K4R2/8/5q2/8/5P2/8/4n3/k7 b - - 0 1");
    print(board);
    std::cout << see(board, Move(E2, F4, CAPTURE)) << std::endl;
    std::cout << see(board, Move(F6, F4, CAPTURE)) << std::endl;
    std::cout << see(board, Move(F6, F8, CAPTURE)) << std::endl;
}

/* Helpers for manipulating pieces on the board */

// Adds a piece pce to board on square sq
inline static void add_piece(board_t *board, piece_t pce, square_t sq) {

    assert(check(board));

    // Add piece to the current position
    // 8x8 board
    board->pieces[sq] = pce;
    // Bitboards
    board->bitboards[pce] |= SQ_TO_BB(sq);
    board->sides_pieces[piece_color(pce)] |= SQ_TO_BB(sq);

    // Hash the piece into the Zobrist key for the board
    board->key ^= piece_keys[pce][sq];
}

// Removes a piece pce from board on square sq
inline static void rm_piece(board_t *board, square_t sq) {

    assert(check(board));
    piece_t pce = board->pieces[sq];

    /* Clear piece off the board */
    // 8x8 board
    board->pieces[sq] = NO_PIECE;
    // Bitboards
    board->bitboards[pce] ^= SQ_TO_BB(sq);
    board->sides_pieces[piece_color(pce)] ^= SQ_TO_BB(sq);

    // Hash the piece out of the Zobrist key for the board
    board->key ^= piece_keys[pce][sq];
}

// Moves a piece pce from 'from' to 'to'
inline static void mv_piece(board_t *board, square_t from, square_t to) {

    /* Move the piece on the board */
    piece_t pce = board->pieces[from];
    // 8x8 board
    board->pieces[to] = pce;
    board->pieces[from] = NO_PIECE;
    // Bitboards
    board->bitboards[pce] ^= SQ_TO_BB(from);
    board->bitboards[pce] |= SQ_TO_BB(to);

    board->sides_pieces[piece_color(pce)] ^= SQ_TO_BB(from);
    board->sides_pieces[piece_color(pce)] |= SQ_TO_BB(to);

    /* Hash the piece out of the old square and into the new */
    board->key ^= piece_keys[pce][from];
    board->key ^= piece_keys[pce][to];
}

/**
 @brief Performs a move, mutating the current board position
 @param board current position
 @param move move to be performed
 @returns True if move was legal, False otherwise
*/
bool make_move(board_t *board, move_t move) {
    assert(check(board));

    // Extract move data
    square_t to = get_to(move);
    square_t from = get_from(move);
    int flags = get_flags(move);

    int me = board->turn;
    int opp = me ^ 1;

    // Store the pre-move state
    /* In C++20 we can do:
    undo_t undo = {
        .move = move,
        .castle_rights = board->castle_rights,
        .ep_square = board->ep_square,
        .fifty_move = board->fifty_move,
        .key = board->key
    };
    */
    undo_t undo = {
        move,
        board->castle_rights,
        board->ep_square,
        board->fifty_move,
        board->key,
        board->pieces[to] // captured piece, if any
    };

    #ifdef DEBUG
    // Store the board state (for debugging purposes)
    //ref_boards[boards++] = *board;
    ref_boards.push_back(*board);
    #endif

    /* Update board state */

    // Handle counters
    ++board->ply;
    ++board->fifty_move; // if a pawn push/captures performed, we'll reset this

    // If en passant performed, remove the captured pawn
    if (flags == EPCAPTURE) {
        // std::cout << "Holy hell!\n";
        square_t target_square = board->ep_square;
        target_square += opp ? NORTH : SOUTH;
        undo.captured = board->pieces[target_square];
        rm_piece(board, target_square);

        // Since a capture was performed, we reset the 50move counter
        board->fifty_move = 0;
    }

    // Save previous board state
    board->history[board->history_ply++] = undo;

    // If castling, move the rook to its new square
    if (flags == KINGCASTLE) {
        if (to == G1) {
            mv_piece(board, H1, F1);
        } else {
            mv_piece(board, H8, F8);
        }
    } else if (flags == QUEENCASTLE) {
        if (to == C1) {
            mv_piece(board, A1, D1);
        } else {
            mv_piece(board, A8, D8);
        }
    }

    /* Handle en passant */
    // Hash out old en passant square (if was set)
    if (board->ep_square != NO_SQ) {
        board->key ^= ep_keys[board->ep_square];
    }

    // Handle new en passant square (only if double pawn push)
    if (flags == PAWNPUSH) {
        board->ep_square = (to + from) >> 1;
        board->key ^= ep_keys[board->ep_square];
    } else {
        board->ep_square = NO_SQ;
    }

    /* Handle castle permissions */
    // Hash out old permissions
    board->key ^= castle_keys[board->castle_rights];

    // Set new permissions
    // (can be spoiled by e.g. moving the king)
    board->castle_rights &= castle_spoils[from];
    board->castle_rights &= castle_spoils[to];

    // Hash in new permissions
    board->key ^= castle_keys[board->castle_rights];

    // Clear any pieces & reset 50move counter if necessary
    if (board->pieces[to] != NO_PIECE) {
        rm_piece(board, to);
        board->fifty_move = 0;
    }

    // If pawn move, reset 50 move counter
    // TODO: No need to do this before in the EPCAPTURE branch?
    if (piece_type(board->pieces[from]) == PAWN) {
        board->fifty_move = 0;
    }

    // Finally, we move the piece
    mv_piece(board, from, to);

    // Handle promotions
    if (is_promotion(move)) {
        // Update the pawn to be the capture choice
        rm_piece(board, to);
        // clear the capture bit
        switch (flags & ~CAPTURE) {
            case ROOKPROMO:
                add_piece(board, set_colour(ROOK, me), to); break;
            case BISHOPPROMO:
                add_piece(board, set_colour(BISHOP, me), to); break;
            case KNIGHTPROMO:
                add_piece(board, set_colour(KNIGHT, me), to); break;
            default: /* QUEENPROMO */
                add_piece(board, set_colour(QUEEN, me), to); break;
        }
    }

    /* TODO: King bitboard should have been updated by mv_piece
    // Update the king square if the king was moved
    std::cout << piece_type(board->pieces[to]) << std::endl;
    std::cout << KING << std::endl;

    if (piece_type(board->pieces[to]) == KING) {
        std::cout << "Updating king square for ";
        std::cout << (me ? "White" : "Black") << " to ";
        std::cout << square_to_str(to) << std::endl;
        // board->king_square[me] = to;
    }
    */

    // Miscellaneous bookkeeping
    board->turn = opp;
    board->key ^= turn_key;

    assert(check(board));

    // Finally, undo the move if puts the player in check (pseudolegal move)
    //if (is_attacked(board, king_square(board, me), opp)) {
    if (is_in_check(board, me)) {
        undo_move(board, move);
        return false;
    }

    return true;
}

void undo_move(board_t *board, move_t move) {
    assert(check(board));

    // Extract move data
    square_t to = get_to(move);
    square_t from = get_from(move);
    int flags = get_flags(move);

    undo_t last = board->history[--board->history_ply];

    // Flip sides
    int me = board->turn;
    int opp = me ^ 1; // The side that performed the move

    board->turn = opp;
    board->key ^= turn_key;

    // Undo the move
    mv_piece(board, to, from);

    // Hash out the en passant square if was currently set
    if (board->ep_square != NO_SQ) {
        board->key ^= ep_keys[board->ep_square];
    }

    // Restore the en passant square from pre-move state
    board->ep_square = last.ep_square;

    // Hash in old en passant square (if any)
    if (board->ep_square != NO_SQ) {
        board->key ^= ep_keys[board->ep_square];
    }

    // If last move was an en passant capture, restore the captured pawn
    if (flags == EPCAPTURE) {
        // std::cout << "New response just dropped\n";
        square_t target_square = board->ep_square;
        target_square += me ? NORTH : SOUTH; // TODO
        add_piece(board, last.captured, target_square);
    } else {
        if (last.captured != NO_PIECE) {
            add_piece(board, last.captured, to); // TODO
        }
    }

    // If castled, move the rook back
    if (flags == KINGCASTLE) {
        if (to == G1) {
            mv_piece(board, F1, H1);
        } else {
            mv_piece(board, F8, H8);
        }
    } else if (flags == QUEENCASTLE) {
        if (to == C1) {
            mv_piece(board, D1, A1);
        } else {
            mv_piece(board, D8, A8);
        }
    }

    /* Handle castling permissions */

    // Hash out current permissions
    board->key ^= castle_keys[board->castle_rights];

    // Restore old castle rights
    board->castle_rights = last.castle_rights;

    // Hash in old castle rights
    board->key ^= castle_keys[board->castle_rights];

    // Restore 50move counter
    board->fifty_move = last.fifty_move;


    /* TODO: King bitboard should have been updated by mv_piece
    // Restore king's square
    if (piece_type(board->pieces[from]) == KING) {
        std::cout << "Updating king square for ";
        std::cout << (opp ? "White" : "Black") << " to ";
        std::cout << square_to_str(from) << std::endl;
        // board->king_square[opp] = from; // TODO Validate side?
    }
    */

    // Undo promotions
    if (is_promotion(move)) {
        // Remove piece that was promoted to
        rm_piece(board, from);
        // Add the pawn back in
        add_piece(board, set_colour(PAWN, opp), from);
    }

    // Update ply counter
    --board->ply;

    /* DEBUG only */
    assert(check(board));

    // Assert that the board matches the previous board
    // on the history stack
    assert(check_against_ref(board));
}

void undo_move(board_t *board) {
    if (board->history_ply < 1)
        return;
    undo_move(board, board->history[board->history_ply - 1].move);
}

/* Null move pruning */

/**
 @brief Performs a null move, mutating the current board position
 Essentialy, we give the opponent a free move
 @param board current position
*/
void make_null(board_t *board) {
    assert(check(board));

    int me = board->turn;
    int opp = me ^ 1;

    // Store the pre-move state
    /* In C++20 we can do:
    undo_t undo = {
        .move = move,
        .castle_rights = board->castle_rights,
        .ep_square = board->ep_square,
        .fifty_move = board->fifty_move,
        .key = board->key
    };
    */
    undo_t undo = {
        NULLMV,
        board->castle_rights,
        board->ep_square,
        board->fifty_move,
        board->key,
        NO_PIECE
    };

    /* Update board state */

    // Handle counters
    ++board->ply;

    // Save previous board state
    board->history[board->history_ply++] = undo;

    /* Handle en passant */
    // Hash out old en passant square (if was set)
    if (board->ep_square != NO_SQ) {
        board->key ^= ep_keys[board->ep_square];
    }
    board->ep_square = NO_SQ;

    // Miscellaneous bookkeeping
    board->turn = opp;
    board->key ^= turn_key;

    assert(check(board));
}

void undo_null(board_t *board) {
    assert(check(board));

    undo_t last = board->history[--board->history_ply];

    // Flip sides
    int me = board->turn;
    int opp = me ^ 1; // The side that performed the null move

    board->turn = opp;
    board->key ^= turn_key;

    // Restore the en passant square from pre-move state
    board->ep_square = last.ep_square;

    // Hash in old en passant square (if any)
    if (board->ep_square != NO_SQ) {
        board->key ^= ep_keys[board->ep_square];
    }

    // Restore old castle rights
    board->castle_rights = last.castle_rights;

    // (TODO: unnecessary? same in make_null()) Restore 50move counter
    board->fifty_move = last.fifty_move;

    // Update ply counter
    --board->ply;

    /* DEBUG only */
    assert(check(board));
}




#ifdef DEBUG

// Prints a shortened board stack info for the last n positions in the format:
// [MOVE] : Key: [key]
//        : Castling rights: [castle_rights]
//        : En passant square: [ep_square]
//        : Fifty move counter: [fifty_move]
//        : Last captured piece: [captured]
void history_trace(const board_t *b, size_t n) {
    // Ensure that n does not exceed the number of positions in the history
    n = std::min(n, static_cast<size_t>(b->history_ply));

    for (size_t i = b->history_ply - n; i < b->history_ply; ++i) {
      const undo_t &undo = b->history[i];
      const move_t &move = undo.move;
      std::cout << std::hex << std::showbase;
      std::cout << "[" << move_to_str(move) << "] : Key: " << b->key << std::dec
                << std::endl;
      std::cout << std::setw(6) << ""
                << " : Castling rights: "
                << castling_rights_to_str(b->castle_rights) << std::endl;
      std::cout << std::setw(6) << ""
                << " : En passant square: "
                << (b->ep_square == NO_SQ ? "-"
                                              : square_to_str(b->ep_square))
                << std::endl;
      std::cout << std::setw(6) << ""
                << " : Fifty move counter: " << b->fifty_move << std::endl;
      std::cout << std::setw(6) << ""
                << " : Last captured piece: " << piece_to_ascii[undo.captured]
                << std::endl;
    }
}

// Useful for testing move generation and hashing
// Called in undo_move if in DEBUG mode, to verify
// whether board state was correctly restored
bool check_against_ref(const board_t *b) {

    // Reference board to compare to
    board_t test = ref_boards.back();
    board_t *ref_b = &test;

    // std::cout << "Reference board:" << std::endl;
    // print(ref_b);

    // Bitboards match
    //assert(std::equal(b->bitboards, b->bitboards + PIECE_NO, ref_b->bitboards));
    for (int i = 0; i < PIECE_NO; ++i) {
        if (b->bitboards[i] != ref_b->bitboards[i]) {
            std::cout << "Mismatch at piece " << piece_to_ascii[i] << std::endl;
            printBB(b->bitboards[i]);
            std::cout << "vs" << std::endl;
            printBB(ref_b->bitboards[i]);
            assert(false);
        }
    }

    // 8x8 boards match
    //assert(std::equal(b->pieces, b->pieces + SQUARE_NO, ref_b->pieces));
    for (square_t sq = A1; sq <= H8; ++sq) {
        if (b->pieces[sq] != ref_b->pieces[sq]) {
            std::cout << "Mismatch at square " << square_to_str(sq) << std::endl;
            std::cout << "Got " << piece_to_ascii[b->pieces[sq]] \
                << " but expected " << piece_to_ascii[ref_b->pieces[sq]] \
                << std::endl;

            print(b);
            print(ref_b);
            assert(false);
        }
    }

    // Side's pieces bitboards match
    assert(b->sides_pieces[WHITE] == ref_b->sides_pieces[WHITE]);
    assert(b->sides_pieces[BLACK] == ref_b->sides_pieces[BLACK]);

    // Side to play matches
    assert(b->turn == ref_b->turn);

    // Counters match
    assert(b->ply == ref_b->ply);
    assert(b->history_ply == ref_b->history_ply);
    assert(b->fifty_move == ref_b->fifty_move);

    // Castle rights match
    assert(b->castle_rights == ref_b->castle_rights);

    // Squares match
    assert(b->ep_square == ref_b->ep_square);
    assert(king_square(b, WHITE) == king_square(ref_b, WHITE));
    assert(king_square(b, BLACK) == king_square(ref_b, BLACK));


    // Zobrist keys match
    if ((uint64_t)b->key != (uint64_t)ref_b->key) {
        std::cout << "Board key: " << b->key;
        std::cout << " (expected " << generate_pos_key(b) << ")" << std::endl;
        std::cout << "Refer key: " << ref_b->key;
        std::cout << " (expected " << generate_pos_key(ref_b) << ")" << std::endl;
        assert(false);
    }

    ref_boards.pop_back();

    return true;
}

/* Verifies that the position is valid (useful for debugging) */
bool check(const board_t *board) {

    assert(board->turn == WHITE || board->turn == BLACK);

    // Verify the board Zobrist key (critical for TT functionality)
    uint64_t expected = generate_pos_key(board);
    if (expected != board->key) {
      std::cout << std::hex << "Expected: " << expected \
                << "\nGot: " << board->key << std::dec << std::endl;
      print(board);
      return false;
    }

    assert(board->ep_square == NO_SQ ||
           (SQUARE_RANK(board->ep_square) == RANK_3 && board->turn == BLACK) ||
           (SQUARE_RANK(board->ep_square) == RANK_6 && board->turn == WHITE));

    // history_trace(board, 5);
    assert((1ULL << king_square(board, WHITE)) == board->bitboards[K]);
    assert(board->castle_rights >= 0 && board->castle_rights <= 15);

    return true;
}

/**
 * Move generation testing with perft()
 * TODO: Automate verifying the results
*/
void perft_test(board_t *board, const std::string& epd_filename) {
  std::fstream epdfile;
  epdfile.open(epd_filename, std::ios::in);

  int time = 100000;
  if (epdfile.is_open()) {
    std::string fenline;
    while (getline(epdfile, fenline)) {
      std::cout << "Testing position " << fenline << std::endl;

      setup(board, fenline);
      for (int depth = 0; depth < 6; ++depth) {
          // std::cout << perft(board, depth) << std::endl;
          mirror_test(board);
      }
    }
    epdfile.close();
  } else {
    std::cout << "File couldn't be opened" << std::endl;
  }
  std::cout << "Perft test finished!\n" << std::endl;
}


void mirror_suite(board_t *board) {
    assert(check(board));

    setup(board, kiwipete_FEN);
    mirror_test(board);
    mirror_test(board);

    setup(board, test1_FEN);
    mirror_test(board);
    mirror_test(board);

    setup(board, test2_FEN);
    mirror_test(board);
    mirror_test(board);

    setup(board, test3_FEN);
    mirror_test(board);
    mirror_test(board);

    setup(board, test4_FEN);
    mirror_test(board);
    mirror_test(board);

    setup(board, test5_FEN);
    mirror_test(board);
    mirror_test(board);

    assert(check(board));
}


#endif // DEBUG
