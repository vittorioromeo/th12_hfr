/* Dear ImGui build configuration.
 *
 * The overlay must never be able to take the game down. Dear ImGui reports misuse through
 * IM_ASSERT, which by default calls assert() and aborts a windowed process with no message
 * at all. Route it to the log instead and switch the menu off, so a mistake in the overlay
 * costs the menu rather than the run.
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void hfr_imgui_failed(const char* expr, const char* file, int line);
#ifdef __cplusplus
}
#endif
#define IM_ASSERT(expr) do { if (!(expr)) hfr_imgui_failed(#expr, __FILE__, __LINE__); } while (0)
#define IMGUI_DISABLE_DEMO_WINDOWS
#define IMGUI_DISABLE_DEBUG_TOOLS
