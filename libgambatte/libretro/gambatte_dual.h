#ifndef GAMBATTE_LIBRETRO_DUAL_H
#define GAMBATTE_LIBRETRO_DUAL_H

#include <stddef.h>
#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define GAMBATTE_DUAL_ABI_VERSION 2u
#define GAMBATTE_DUAL_SUBSYSTEM_ID 0x47424c43u /* "GBLC" */

enum gambatte_dual_capability {
   GAMBATTE_DUAL_CAP_TWO_CONTENTS       = 1u << 0,
   GAMBATTE_DUAL_CAP_CONSOLE_MEMORY     = 1u << 1,
   GAMBATTE_DUAL_CAP_VISIBLE_CONSOLE    = 1u << 2,
   GAMBATTE_DUAL_CAP_PAIRED_CHECKPOINT  = 1u << 3,
   GAMBATTE_DUAL_CAP_TARGETED_RESET     = 1u << 4,
   GAMBATTE_DUAL_CAP_CLOCK_EPOCHS       = 1u << 5
};

enum gambatte_dual_console {
   GAMBATTE_DUAL_CONSOLE_A = 0,
   GAMBATTE_DUAL_CONSOLE_B = 1,
   /* Diagnostic presentation only. Netplay should select A or B. */
   GAMBATTE_DUAL_CONSOLE_BOTH = 2
};

/* These symbols exist only in NETPLAY_DUAL_INSTANCE builds. Frontends should
 * resolve retro_dual_get_abi_version first and reject unsupported versions. */
unsigned retro_dual_get_abi_version(void);
uint64_t retro_dual_get_capabilities(void);

bool retro_dual_set_visible_console(unsigned console);
unsigned retro_dual_get_visible_console(void);

/* Power-on time of day for each console's cartridge clock, in seconds since
 * the Unix epoch. Both devices in a paired session must pass the same pair of
 * values - each player's own clock, agreed during the handshake - because a
 * cartridge with an RTC turns the answer into emulated state as soon as the
 * game latches it, and no two wall clocks agree.
 *
 * Time then advances from these by emulated frames rather than by either
 * device's clock, so the two replicas stay identical. Call before loading
 * content; without it both consoles share a fixed epoch and the session is
 * still deterministic, just not showing anybody's real time. */
bool retro_dual_set_clock_epochs(uint64_t console_a, uint64_t console_b);

void *retro_dual_get_memory_data(unsigned console, unsigned id);
size_t retro_dual_get_memory_size(unsigned console, unsigned id);
bool retro_dual_reset_console(unsigned console);

/* Paired checkpoints deliberately exclude local presentation, diagnostics,
 * and cached input. Consequently two devices can hash the returned bytes even
 * though each presents a different console. Checkpoints are accepted only
 * between retro_run calls while the in-process serial bus is idle. */
bool retro_dual_is_checkpoint_safe(void);
size_t retro_dual_serialize_size(void);
bool retro_dual_serialize(void *data, size_t size);
bool retro_dual_unserialize(const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
