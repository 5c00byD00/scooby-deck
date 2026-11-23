# SPDX-License-Identifier: GPL-3.0-or-later
# Scooby Deck: a runtime switch in lv_draw_rect() that draws every rectangle
# square (except LV_RADIUS_CIRCLE), without shadows and without gradients, for
# the terminal theme. WadaMesh sets radii in ~200 places; this flattens them
# all at the one draw entry point. Off (0) draws exactly as stock LVGL.
# Idempotent; fails closed on LVGL drift.
import os

MARKER = "scooby-deck-flat-patch"
OLD = """void lv_draw_rect(lv_draw_ctx_t * draw_ctx, const lv_draw_rect_dsc_t * dsc, const lv_area_t * coords)
{
    if(lv_area_get_height(coords) < 1 || lv_area_get_width(coords) < 1) return;

    draw_ctx->draw_rect(draw_ctx, dsc, coords);
"""
NEW = """/*scooby-deck-flat-patch*/
volatile uint8_t lv_scooby_flat = 0;
void lv_draw_rect(lv_draw_ctx_t * draw_ctx, const lv_draw_rect_dsc_t * dsc, const lv_area_t * coords)
{
    if(lv_area_get_height(coords) < 1 || lv_area_get_width(coords) < 1) return;

    if(lv_scooby_flat) {
        lv_draw_rect_dsc_t flat = *dsc;
        if(flat.radius != LV_RADIUS_CIRCLE) flat.radius = 0;
        flat.shadow_width = 0;
        flat.shadow_opa = LV_OPA_TRANSP;
        flat.bg_grad.dir = LV_GRAD_DIR_NONE;
        draw_ctx->draw_rect(draw_ctx, &flat, coords);
    }
    else {
        draw_ctx->draw_rect(draw_ctx, dsc, coords);
    }
"""

def patch(path):
    src = open(path, encoding="utf-8").read()
    if MARKER in src:
        return False
    if src.count(OLD) != 1:
        raise RuntimeError("lv_draw_rect.c does not match LVGL 8.4")
    open(path, "w", encoding="utf-8").write(src.replace(OLD, NEW, 1))
    return True

try:
    Import("env")  # noqa: F821
except NameError:
    env = None

if env is not None:
    path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "lvgl", "src", "draw", "lv_draw_rect.c")
    if os.path.isfile(path):
        try:
            print("[patch_lvgl_scooby_flat] %s" % ("patched" if patch(path) else "already patched"))
        except RuntimeError as e:
            print("[patch_lvgl_scooby_flat] ERROR: %s" % e)
            env.Exit(1)
    else:
        print("[patch_lvgl_scooby_flat] LVGL not fetched yet; re-run the build")
