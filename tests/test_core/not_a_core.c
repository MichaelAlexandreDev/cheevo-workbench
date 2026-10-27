/* A shared library that is not a libretro core: it exports none of the retro_* functions.
   Used to check that LibretroCore rejects it with a clear message. */
int cw_not_a_core(void) {
    return 0;
}
