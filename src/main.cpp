#include <iostream>

#include "types.h"
#include "board.h"
#include "bitboard.h"
#include "uci.h"
#include "attack.h"

int main(int argc, char* argv[]) {

    // Print engine info
    std::cout << NAME << " by " << AUTHOR << std::endl;
    std::cout << "Built on " << __TIMESTAMP__ << std::endl;

    // Initialization
    init_eval_masks();
    init_keys();
    init_pawn_attacks();
    init_leap_attacks();
    init_bishop_occupancies();
    init_rook_occupancies();
    init_magics<BISHOP>();
    init_magics<ROOK>();
    init_slider_attacks();

    // Start UCI driver loop
    loop(argc, argv);

    return 0;
}
