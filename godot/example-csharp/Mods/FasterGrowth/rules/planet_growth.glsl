// The mod's growth: as the game's, but unrest also lowers stability.
void main() {
    FOR_EACH_PLANET(p) {
        int housing = planet_housing[p];
        int pop = planet_pop[p];
        int free_housing = fx_max(housing - pop, 0);
        int growth = D.growth_rate + planet_mod_pop_growth(p);
        pop += fx_mul(fx_mul(pop, growth), fx_div(free_housing, housing));
        if (rng_below(rng_roll(p, terni.tick, 1u), D.unrest_chance)) {
            pop -= fx_mul(pop, D.unrest_loss);
            planet_unrest_events[p] += 1u;
            planet_stability[p] = fx_max(planet_stability[p] - FX_ONE, 0);
        }
        planet_pop[p] = pop;
        planet_tax[p] = fx_mul(fx_mul(pop, D.tax_rate), FX_ONE + planet_mod_tax_efficiency(p));
    }
}
