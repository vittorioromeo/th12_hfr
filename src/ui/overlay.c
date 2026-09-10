/* ------------------------------------------------------------------ in-game overlay
 * Drawn by the scaler inside its own BeginScene, after the game's image has been placed in
 * the back buffer. `content` is where the game's picture landed, so the overlay can align
 * itself to the picture rather than to the letterbox. Replaced by the ImGui menu.
 */
static void ui_render_frame(IDirect3DDevice9* dev, const struct ScaleRect* content) {
    (void)dev; (void)content;
}
