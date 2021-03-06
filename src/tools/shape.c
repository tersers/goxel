/* Goxel 3D voxels editor
 *
 * copyright (c) 2017 Guillaume Chereau <guillaume@noctua-software.com>
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.

 * Goxel is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.

 * You should have received a copy of the GNU General Public License along with
 * goxel.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "goxel.h"


typedef struct {
    tool_t tool;
    vec3_t start_pos;
    mesh_t *mesh_orig;
    bool   adjust;

    struct {
        gesture3d_t drag;
        gesture3d_t hover;
        gesture3d_t adjust;
    } gestures;

} tool_shape_t;


static box_t get_box(const vec3_t *p0, const vec3_t *p1, const vec3_t *n,
                     float r, const plane_t *plane)
{
    mat4_t rot;
    box_t box;
    if (p1 == NULL) {
        box = bbox_from_extents(*p0, r, r, r);
        box = box_swap_axis(box, 2, 0, 1);
        return box;
    }
    if (r == 0) {
        box = bbox_grow(bbox_from_points(*p0, *p1), 0.5, 0.5, 0.5);
        // Apply the plane rotation.
        rot = plane->mat;
        rot.vecs[3] = vec4(0, 0, 0, 1);
        mat4_imul(&box.mat, rot);
        return box;
    }

    // Create a box for a line:
    int i;
    const vec3_t AXES[] = {vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)};

    box.mat = mat4_identity;
    box.p = vec3_mix(*p0, *p1, 0.5);
    box.d = vec3_sub(*p1, box.p);
    for (i = 0; i < 3; i++) {
        box.w = vec3_cross(box.d, AXES[i]);
        if (vec3_norm2(box.w) > 0) break;
    }
    if (i == 3) return box;
    box.w = vec3_mul(vec3_normalized(box.w), r);
    box.h = vec3_mul(vec3_normalized(vec3_cross(box.d, box.w)), r);
    return box;
}

static int on_hover(gesture3d_t *gest, void *user)
{
    box_t box;
    cursor_t *curs = gest->cursor;
    uvec4b_t box_color = HEXCOLOR(0xffff00ff);

    goxel_set_help_text(goxel, "Click and drag to draw.");
    box = get_box(&curs->pos, &curs->pos, &curs->normal, 0,
                  &goxel->plane);
    render_box(&goxel->rend, &box, &box_color, EFFECT_WIREFRAME);
    return 0;
}

static int on_drag(gesture3d_t *gest, void *user)
{
    tool_shape_t *shape = user;
    mesh_t *mesh = goxel->image->active_layer->mesh;
    box_t box;
    cursor_t *curs = gest->cursor;

    if (shape->adjust) return GESTURE_FAILED;

    if (gest->state == GESTURE_BEGIN) {
        mesh_set(shape->mesh_orig, mesh);
        shape->start_pos = curs->pos;
        image_history_push(goxel->image);
    }

    goxel_set_help_text(goxel, "Drag.");
    box = get_box(&shape->start_pos, &curs->pos, &curs->normal,
                  0, &goxel->plane);
    mesh_set(mesh, shape->mesh_orig);
    mesh_op(mesh, &goxel->painter, &box);
    goxel_update_meshes(goxel, MESH_RENDER);

    if (gest->state == GESTURE_END) {
        goxel_update_meshes(goxel, -1);
        shape->adjust = goxel->tool_shape_two_steps;
    }
    return 0;
}

static int on_adjust(gesture3d_t *gest, void *user)
{
    tool_shape_t *shape = user;
    cursor_t *curs = gest->cursor;
    vec3_t pos;
    box_t box;
    mesh_t *mesh = goxel->image->active_layer->mesh;

    goxel_set_help_text(goxel, "Adjust height.");

    if (gest->state == GESTURE_BEGIN) {
        goxel->tool_plane = plane_from_normal(curs->pos,
                                              goxel->plane.u);
    }

    pos = vec3_add(goxel->tool_plane.p,
                   vec3_project(vec3_sub(curs->pos, goxel->tool_plane.p),
                                goxel->plane.n));
    pos.x = round(pos.x - 0.5) + 0.5;
    pos.y = round(pos.y - 0.5) + 0.5;
    pos.z = round(pos.z - 0.5) + 0.5;

    box = get_box(&shape->start_pos, &pos, &curs->normal, 0,
                  &goxel->plane);

    mesh_set(mesh, shape->mesh_orig);
    mesh_op(mesh, &goxel->painter, &box);
    goxel_update_meshes(goxel, MESH_RENDER);

    if (gest->state == GESTURE_END) {
        goxel->tool_plane = plane_null;
        mesh_set(shape->mesh_orig, mesh);
        shape->adjust = false;
        goxel_update_meshes(goxel, -1);
    }

    return 0;
}

static int iter(tool_t *tool, const vec4_t *view)
{
    tool_shape_t *shape = (tool_shape_t*)tool;
    cursor_t *curs = &goxel->cursor;
    curs->snap_offset = (goxel->painter.mode == MODE_OVER) ? 0.5 : -0.5;

    if (!shape->mesh_orig)
        shape->mesh_orig = mesh_copy(goxel->image->active_layer->mesh);

    if (!shape->gestures.drag.type) {
        shape->gestures.drag = (gesture3d_t) {
            .type = GESTURE_DRAG,
            .callback = on_drag,
        };
        shape->gestures.hover = (gesture3d_t) {
            .type = GESTURE_HOVER,
            .callback = on_hover,
        };
        shape->gestures.adjust = (gesture3d_t) {
            .type = GESTURE_HOVER,
            .callback = on_adjust,
        };
    }

    gesture3d(&shape->gestures.drag, curs, shape);
    if (!shape->adjust)
        gesture3d(&shape->gestures.hover, curs, shape);
    else
        gesture3d(&shape->gestures.adjust, curs, shape);

    return tool->state;
}


static int gui(tool_t *tool)
{
    tool_gui_smoothness();
    gui_checkbox("Two steps", &goxel->tool_shape_two_steps,
                 "Second click set the height");
    tool_gui_snap();
    tool_gui_mode();
    tool_gui_shape();
    tool_gui_color();
    tool_gui_symmetry();

    return 0;
}

TOOL_REGISTER(TOOL_SHAPE, shape, tool_shape_t,
              .iter_fn = iter,
              .gui_fn = gui,
              .flags = TOOL_REQUIRE_CAN_EDIT,
              .shortcut = "S",
)
