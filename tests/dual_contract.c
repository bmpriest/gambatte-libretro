#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"
#include "gambatte_dual.h"

static unsigned last_width;
static unsigned subsystem_id;
static unsigned subsystem_roms;

static bool environment(unsigned cmd, void *data) {
   switch (cmd) {
      case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO: {
         const struct retro_subsystem_info *s = data;
         if (s && s[0].ident && !strcmp(s[0].ident, "gblc")) {
            subsystem_id = s[0].id;
            subsystem_roms = s[0].num_roms;
         }
         return true;
      }
      case RETRO_ENVIRONMENT_GET_CAN_DUPE:
         *(bool*)data = true;
         return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_VARIABLES:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool*)data = false;
         return true;
      default:
         return false;
   }
}

static void video(const void *data, unsigned width, unsigned height, size_t pitch) {
   (void)data; (void)height; (void)pitch; last_width = width;
}
static void audio(int16_t left, int16_t right) { (void)left; (void)right; }
static size_t audio_batch(const int16_t *data, size_t frames) { (void)data; return frames; }
static void input_poll(void) {}
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
   (void)port; (void)device; (void)index; (void)id; return 0;
}

static void *read_file(const char *path, size_t *size) {
   FILE *file = fopen(path, "rb");
   if (!file) return NULL;
   fseek(file, 0, SEEK_END);
   long length = ftell(file);
   fseek(file, 0, SEEK_SET);
   void *data = length > 0 ? malloc((size_t)length) : NULL;
   if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
      free(data); data = NULL;
   }
   fclose(file);
   *size = data ? (size_t)length : 0;
   return data;
}

#define RESOLVE(type, name) __typeof__((type)0) name = (type)dlsym(handle, #name)
#define REQUIRE(expr) do { if (!(expr)) { \
   fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

int main(int argc, char **argv) {
   if (argc != 4) {
      fprintf(stderr, "usage: %s <dual-core.so> <console-a.gb> <console-b.gb>\n", argv[0]);
      return 2;
   }
   void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   REQUIRE(handle != NULL);
   RESOLVE(void (*)(retro_environment_t), retro_set_environment);
   RESOLVE(void (*)(retro_video_refresh_t), retro_set_video_refresh);
   RESOLVE(void (*)(retro_audio_sample_t), retro_set_audio_sample);
   RESOLVE(void (*)(retro_audio_sample_batch_t), retro_set_audio_sample_batch);
   RESOLVE(void (*)(retro_input_poll_t), retro_set_input_poll);
   RESOLVE(void (*)(retro_input_state_t), retro_set_input_state);
   RESOLVE(void (*)(void), retro_init);
   RESOLVE(void (*)(void), retro_deinit);
   RESOLVE(void (*)(void), retro_run);
   RESOLVE(bool (*)(unsigned, const struct retro_game_info*, size_t), retro_load_game_special);
   RESOLVE(void (*)(void), retro_unload_game);
   RESOLVE(size_t (*)(void), retro_serialize_size);
   RESOLVE(unsigned (*)(void), retro_dual_get_abi_version);
   RESOLVE(uint64_t (*)(void), retro_dual_get_capabilities);
   RESOLVE(bool (*)(unsigned), retro_dual_set_visible_console);
   RESOLVE(void *(*)(unsigned, unsigned), retro_dual_get_memory_data);
   RESOLVE(size_t (*)(unsigned, unsigned), retro_dual_get_memory_size);
   RESOLVE(bool (*)(void), retro_dual_is_checkpoint_safe);
   RESOLVE(size_t (*)(void), retro_dual_serialize_size);
   RESOLVE(bool (*)(void*, size_t), retro_dual_serialize);
   RESOLVE(bool (*)(const void*, size_t), retro_dual_unserialize);

   REQUIRE(retro_set_environment && retro_init && retro_run &&
      retro_load_game_special && retro_dual_get_abi_version);
   retro_set_environment(environment);
   REQUIRE(subsystem_id == GAMBATTE_DUAL_SUBSYSTEM_ID && subsystem_roms == 2);
   retro_set_video_refresh(video);
   retro_set_audio_sample(audio);
   retro_set_audio_sample_batch(audio_batch);
   retro_set_input_poll(input_poll);
   retro_set_input_state(input_state);
   retro_init();
   REQUIRE(retro_dual_get_abi_version() == GAMBATTE_DUAL_ABI_VERSION);
   REQUIRE((retro_dual_get_capabilities() & GAMBATTE_DUAL_CAP_TWO_CONTENTS) != 0);
   REQUIRE(retro_dual_set_visible_console(GAMBATTE_DUAL_CONSOLE_B));

   size_t sizes[2];
   void *roms[2] = { read_file(argv[2], &sizes[0]), read_file(argv[3], &sizes[1]) };
   REQUIRE(roms[0] && roms[1]);
   struct retro_game_info games[2] = {
      { argv[2], roms[0], sizes[0], NULL },
      { argv[3], roms[1], sizes[1], NULL }
   };
   REQUIRE(retro_load_game_special(GAMBATTE_DUAL_SUBSYSTEM_ID, games, 2));
   REQUIRE(!retro_dual_set_visible_console(GAMBATTE_DUAL_CONSOLE_A));
   REQUIRE(retro_serialize_size() == 0);
   REQUIRE(retro_dual_get_memory_data(0, RETRO_MEMORY_SYSTEM_RAM) !=
      retro_dual_get_memory_data(1, RETRO_MEMORY_SYSTEM_RAM));
   REQUIRE(retro_dual_get_memory_size(0, RETRO_MEMORY_SYSTEM_RAM) >= 0x2000);
   REQUIRE(retro_dual_get_memory_size(1, RETRO_MEMORY_SYSTEM_RAM) >= 0x2000);

   for (unsigned i = 0; i < 10; ++i) retro_run();
   REQUIRE(last_width == 160);
   REQUIRE(retro_dual_is_checkpoint_safe());
   const size_t state_size = retro_dual_serialize_size();
   REQUIRE(state_size > 16);
   void *state = malloc(state_size);
   void *state_copy = malloc(state_size);
   REQUIRE(state && state_copy && retro_dual_serialize(state, state_size));
   REQUIRE(retro_dual_serialize(state_copy, state_size));
   REQUIRE(memcmp(state, state_copy, state_size) == 0);
   unsigned char *ram_b = retro_dual_get_memory_data(1, RETRO_MEMORY_SYSTEM_RAM);
   REQUIRE(ram_b != NULL);
   const unsigned char original = ram_b[0];
   ram_b[0] ^= 0xff;
   REQUIRE(retro_dual_unserialize(state, state_size));
   REQUIRE(ram_b[0] == original);

   free(state_copy); free(state);
   retro_unload_game();
   retro_deinit();
   free(roms[0]); free(roms[1]);
   dlclose(handle);
   puts("dual frontend contract: ok");
   return 0;
}
