// Planet taxes -> owner's income (integer sum: order-independent, deterministic).
void main() {
    FOR_EACH_PLANET(p) {
        atomicAdd(empire_income[planet_owner[p]], to_agg(planet_tax[p]));
    }
}
