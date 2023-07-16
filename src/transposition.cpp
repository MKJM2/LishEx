#include "transposition.h"

#include <cstring> // std::memset

#include "search.h"
#include "movegen.h"

// Default TT size in MB
constexpr int TTSIZEMB = 64;

// Global transposition table
TT tt(TTSIZEMB);

TT::TT(const int MB) : size{(0x100000 * MB / sizeof(tt_entry)) - 2} {

    table = new tt_entry[size];
    if (table == nullptr) {
        std::cerr << "Transposition table allocation failed!\n";
        assert(false);
    }
    this->clear();
    std::cout << "Allocated TT of size " << MB << "MB " \
              << "with " << size << " entries (entry size is " \
              << sizeof(tt_entry) << ')' << std::endl;
}

TT::~TT() {
    //if (table != nullptr) {
        //delete[] table;
    //}

    // delete is safe to use on nullptrs
    delete[] table;
}

void TT::reset_stats() {
    overwrites = hit = cut = 0U;
}

void TT::clear() {
    // std::memset(table, 0, this->size * sizeof(tt_entry));
    // std::fill(table, table + size, tt_entry());
    tt_entry *entry;
    for (entry = table; entry < table + size; ++entry) {
        entry->key = 0ULL;
        entry->depth = 0;
        entry->flags = BAD;
        entry->move = NULLMV;
        entry->score = 0;
    }
    reset_stats();
}

int TT::probe(const board_t *board, tt_entry *entry, move_t *move, int *score, int alpha, int beta, int depth) {

    // TODO: Ensure size of TT is a power of 2 for efficient mod with &
    size_t idx = board->key % this->size;
    *entry = table[idx];

    assert(0 <= idx && idx <= this->size - 1);
    assert(1 <= depth && depth < MAX_DEPTH);
    assert(alpha < beta);
    assert(-oo <= alpha && alpha <= +oo);
    assert(-oo <= beta && beta <= +oo);
    assert(0 <= board->ply && board->ply < MAX_DEPTH);

    /* Check if the zobrist key matches */
    if (entry->key != board->key)
        return TTMISS;

    /* We have a match! Check if search was deep enough */

    // The move stored might be useful for move ordering
    *move = static_cast<move_t>(entry->move);

    // If the previous search wasn't as deep as current
    if (depth > static_cast<int>(entry->depth))
        return TTMISS;

    assert(1 <= entry->depth && entry->depth < MAX_DEPTH);

    // Otherwise, we've hit a valid entry!
    ++hit;

    // We overwrite the score
    *score = static_cast<int>(entry->score);

    // Adjust for mate scores
    if (*score > +oo - MAX_DEPTH)
        score -= board->ply;
    else if (*score < -oo + MAX_DEPTH)
        score += board->ply;

    /**
     * If we have an EXACT entry, the score was within the [alpha, beta]
     * window and the move stored in the entry is a good move to try.
     * If we have a LOWER entry, it was stored after a fail-high (Beta) cutoff,
     * meaning the move stored in the entry caused a beta cutoff and
     * the score stored is a probable LOWERbound of the actual score. If the
     * score is greater than our current upperbound (beta) we clamp it.
     * Otherwise, if the entry is an UPPER entry, the entry score is only
     * an upperbound on the score of the position and all moves failed.
     * */

    /* The entry can be useful. Check it's type */
    switch (static_cast<int>(entry->flags)) {
        case LOWER: *score = MIN(*score, beta); break;
        case UPPER: *score = MAX(*score, alpha); break;
        case EXACT: break;
        case BAD: LOG("TODO: Not handled yet"); assert(false);
    }
    return TTHIT;
}

// Always-replace replacement scheme
void TT::store(const board_t *board, move_t move, int score,
               const int flags, const int depth) {

    // TODO: Same as in probe, make sure TT is a power of 2
    size_t idx = board->key % this->size;
    tt_entry *entry = &table[idx];

    assert(0 <= idx && idx <= this->size - 1);
    assert(board->key == generate_pos_key(board));
    assert(depth >= 1);
    assert(BAD <= flags && flags <= EXACT);

    if (entry->key == 0ULL)
        ++writes;
    else
        ++overwrites;

    // Mate score logic for returning how many plies from mate
    if (score > +oo - MAX_DEPTH) score += board->ply;
    else if (score < -oo + MAX_DEPTH) score -= board->ply;

    // If all moves failed high, we don't know which move is the best,
    // hence the stored move should be empty
    // assert(flags == UPPER ? move == NULLMV : move != NULLMV);

    /* Finally, store the entry in the transposition table */
    entry->key   = board->key;
    entry->depth = static_cast<uint8_t>(depth & UINT8_MAX);
    entry->flags = static_cast<uint8_t>(flags & UINT8_MAX);
    entry->move  = static_cast<uint16_t>(move & UINT16_MAX); // only store the 16 least-significant bits
    entry->score = static_cast<int32_t>(score);

    // Debug: assert the casts are correct
    assert(entry->key == board->key);
    assert(entry->depth == depth);
    assert(entry->flags == flags);
    assert(entry->move == move);
    assert(entry->score == score);
}


/* Adapted from VICE by Bluefever Software */

move_t TT::probe_pv(const board_t *board) {
    size_t idx = board->key % this->size;
    tt_entry *entry = &table[idx];

    if (entry->key == board->key) {
        return entry->move;
    }
    return NULLMV;
}


int TT::get_pv_line(board_t *board, const int depth) {

    move_t move = NULLMV;
    int count = 0;

    while ((move = probe_pv(board)) != NULLMV && count < depth) {
        if (move_exists(board, move)) {
            make_move(board, move);
            board->pv[count++] = move;
        } else {
            break;
        }
    }

    while (board->ply > 0) {
        undo_move(board);
    }

    return count;
}
