// rs_rldpack.c - rldpack inside Reload Studio
//
// The same source file as the command-line tool, only its main is called
// Rldpack_Main here. rs_shell.c calls it when the exe starts with
// `--rldpack <arguments>`. So there is ONE file for the author and ONE
// implementation of every check - Reload Studio checks nothing itself, it
// only shows what rldpack says (tools/reloadstudio/reloadstudio.h, the section on
// the rldpack pipe).
//
// The one thing taken from here directly is a picture, not a check: the
// reference dummy that make-char fits every model onto (tools/rldpack_dummy.inc,
// pulled in by tools/rldpack_char.inc). Its functions are static in this
// translation unit, so the preview (rs_view.c) gets it through Rs_DummyMesh
// below - the numbers rldpack builds with, not a copy of them.

#define main Rldpack_Main
#include "../rldpack.c"
#undef main

#include "rs_view.h"

// Rounded to 1/16 game units, half away from zero. The doubles come from
// RldDum_Mesh and RldDum_ApplyPose, which call no library functions, so the
// same input gives the same integers everywhere.
static int Rs_DummySub(double v)
{
    return v >= 0.0 ? (int)(v * 16.0 + 0.5) : -(int)(-v * 16.0 + 0.5);
}

// See rs_view.h. The preview poses are the frames 10, 0 and 20 of animation 0,
// in the order of the --preview file.
int Rs_DummyMesh(int wheels, int pose, int *position, int positionMax, int *triangle, unsigned int *color,
                 int triangleMax, int *positionCount)
{
    static const u32 frames[RS_VIEW_POSE_COUNT] = { 10u, 0u, 20u };
    struct RldDumPose dumPose;
    double *p;
    u32 *t, *c;
    u32 positions = 0, triangles, first, i;
    int a;

    wheels = wheels != 0;
    if (pose < 0 || pose >= RS_VIEW_POSE_COUNT)
        pose = RS_VIEW_POSE_NEUTRAL;
    triangles = RldDum_Mesh(wheels, NULL, 0u, NULL, NULL, 0u, &positions);
    if (positionCount)
        *positionCount = (int)positions;
    if (!position || !triangle)
        return (int)triangles;
    if (triangles == 0 || positionMax < 0 || triangleMax < 0 || positions > (u32)positionMax ||
        triangles > (u32)triangleMax) {
        if (positionCount)
            *positionCount = 0;
        return 0;
    }

    p = (double *)malloc((size_t)positions * 3u * sizeof(double));
    t = (u32 *)malloc((size_t)triangles * 3u * sizeof(u32));
    c = (u32 *)malloc((size_t)triangles * sizeof(u32));
    if (!p || !t || !c || RldDum_Mesh(wheels, p, positions, t, c, triangles, &positions) != triangles) {
        free(p);
        free(t);
        free(c);
        if (positionCount)
            *positionCount = 0;
        return 0;
    }

    // The steering wheel comes last; it turns about its column as in the frame.
    first = RldDum_MeshSteeringFirst(wheels);
    RldDum_PoseFrame(frames[pose], &dumPose);
    for (i = 0; i < positions; i++) {
        double at[3];
        if (i >= first)
            RldDum_ApplyPose(&dumPose, RLDDUM_GROUP_WHEEL, p + 3u * i, at);
        else
            memcpy(at, p + 3u * i, sizeof(at));
        for (a = 0; a < 3; a++)
            position[3u * i + (u32)a] = Rs_DummySub(at[a]);
    }
    for (i = 0; i < 3u * triangles; i++)
        triangle[i] = (int)t[i];
    if (color)
        for (i = 0; i < triangles; i++)
            color[i] = c[i];

    free(p);
    free(t);
    free(c);
    return (int)triangles;
}
