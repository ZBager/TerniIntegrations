// income -> treasury (64-bit value from two 32-bit words) and food stock.
void main() {
    FOR_EACH_EMPIRE(e) {
        empire_treasury[e] = add_i64(empire_treasury[e], empire_income[e]);
        empire_stock[e * EMPIRE_STOCK_SIZE + 0u] += 1;
    }
}
