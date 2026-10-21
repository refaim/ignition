/*
 * phys.c - a car's rigid body (0x40e6b0..0x40ffa0), in metres and
 * seconds. A step is 1/72 s: the race steps the physics twice a tick.
 */

#include <math.h>

#include "phys.h"

/* 0x40e6b0
 * One step of the body. On the ground, the surface's traction and grip
 * (times the weight on the ground, less on a slope) limit the drive
 * force and how much of the sideways speed is taken away; the yaw rate
 * chases the steering's (or, braking, the skid's) at a rate the
 * traction allows. In the air only the yaw and the speed carry on.
 * `slope` scales the pull of gravity along the slopes (1.0 or 4.0). */
void phys_step(struct car_phys *p, int airborne, double slope)
{
    double drive = p->drive, heading = p->heading;
    double travel = 0.0, target, limit;
    double c, s, cs, ss;

    if (airborne) {
        double side = p->heading + 1.570798;

        p->lateral = cos(side) * p->vx + sin(side) * p->vz;
        p->traction_force = 0;
        p->grip_force = 0;
        p->slipping = 1;
        p->speed = cos(p->heading) * p->vx + sin(p->heading) * p->vz;
        p->heading = p->yaw_rate * 0.013888888888888888 + p->heading;
        c = cos(p->heading);
        s = sin(p->heading);
        p->x = cos(atan2(tan(p->roll) * c + s * tan(p->pitch), 1.0)) * p->vx
                * 0.013888888888888888 + p->x;
        p->z = cos(atan2(tan(p->pitch) * c + s * tan(p->roll), 1.0)) * p->vz
                * 0.013888888888888888 + p->z;
        return;
    }

    {
        double weight = cos(p->pitch) * cos(p->roll);

        p->traction_force = p->traction * p->mass * weight * 9.81;
        p->grip_force = weight * (p->grip * p->mass) * 9.81;
    }
    p->speed = sin(p->heading) * p->vz + cos(p->heading) * p->vx;
    if (!p->brake) {
        target = tan(p->steer) / (p->rear_len + p->front_len) * p->speed;
        limit = p->traction_force / p->inertia * 0.041666666666666664;
    } else {
        /* A skid turns the car towards where it is going. */
        double v;

        if (p->vz != 0.0 || p->vx != 0.0) {
            travel = atan2(p->vz, p->vx);
        }
        v = sqrt(p->vz * p->vz + p->vx * p->vx);
        target = sin(heading - travel) * v * 0.2;
        limit = p->traction_force / p->inertia * 0.027777777777777776;
    }
    if (fabs(p->yaw_rate - target) <= limit) {
        p->yaw_rate = target;
    } else if (p->yaw_rate < target) {
        p->yaw_rate += limit;
    } else {
        p->yaw_rate -= limit;
    }
    p->slipping = 0;
    p->heading = p->yaw_rate * 0.013888888888888888 + p->heading;
    c = cos(p->heading);
    s = sin(p->heading);
    cs = cos(p->heading + 1.570798);
    ss = sin(p->heading + 1.570798);
    if (!p->brake) {
        double ahead, across, side;

        if (p->traction_force <= drive) {
            drive = p->traction_force;
            p->slipping = 1;
        }
        ahead = sin(p->roll) * slope * 9.81 + drive / p->mass;
        p->vx = (cs * sin(p->pitch) * slope * 9.81 + c * ahead) * 0.013888888888888888 + p->vx;
        p->vz = (ahead * s + ss * sin(p->pitch) * slope * 9.81) * 0.013888888888888888 + p->vz;
        /* The sideways speed, taken away up to the grip. */
        across = p->grip_force / p->mass * 0.013888888888888888;
        side = p->vz * ss + p->vx * cs;
        p->lateral = side;
        if (fabs(side) < across) {
            across = fabs(side);
        }
        if (!(0.0 <= side)) {
            across = -across;
        }
        p->vx -= across * cs;
        p->vz -= across * ss;
    } else {
        double decel, v = sqrt(p->vz * p->vz + p->vx * p->vx);

        p->vx = ((cs * sin(p->pitch) * 9.81 + c * sin(p->roll) * 9.81) * slope)
                * 0.013888888888888888 + p->vx;
        p->vz = ((sin(p->pitch) * ss * 9.81 + s * sin(p->roll) * 9.81) * slope)
                * 0.013888888888888888 + p->vz;
        /* Braked, the grip slows the car along its travel. */
        decel = p->grip_force / p->mass * 0.013888888888888888;
        if (!(decel <= v)) {
            decel = v;
        }
        p->vx -= cos(travel) * decel;
        p->vz -= sin(travel) * decel;
    }
    p->x = cos(atan2(c * tan(p->roll) + s * tan(p->pitch), 1.0)) * p->vx
            * 0.013888888888888888 + p->x;
    p->z = cos(atan2(tan(p->pitch) * c + tan(p->roll) * s, 1.0)) * p->vz
            * 0.013888888888888888 + p->z;
}

/* 0x40fee0: the body's corners in the world, as they are and spread
 * by `scale` (faster cars probe the ground further out). */
void phys_corners(struct car_phys *p, double scale)
{
    double c = cos(p->heading), s = sin(p->heading);
    int k;

    for (k = 0; k < 4; k++) {
        double rx = c * p->corner_x[k] - s * p->corner_z[k];
        double rz = s * p->corner_x[k] + c * p->corner_z[k];

        p->corner_world[2 * k] = p->x + rx;
        p->corner_world[2 * k + 1] = rz + p->z;
        p->corner_scaled[2 * k] = rx * scale + p->x;
        p->corner_scaled[2 * k + 1] = rz * scale + p->z;
    }
}

/* 0x40ffa0 */
static int sign(double x)
{
    if (x < 0.0) {
        return -1;
    }
    return x > 0.0 ? 1 : 0;
}

/* The angle of (dx, dz), 0 for none. */
static double angle_of(double dx, double dz)
{
    return dz == 0.0 && dx == 0.0 ? 0.0 : atan2(dz, dx);
}

/* 0x40ec20
 * Two bodies touching at (cx, cz) across a normal at angle `normal`: if
 * their points there close on each other, an impulse with the bodies'
 * bounciness (f010) turns and pushes each, and each is moved a
 * quarter metre apart. Returns 1 if they hit. */
int phys_impulse(struct car_phys *a, struct car_phys *b, double cx, double cz, double normal)
{
    double e = a->f010 * b->f010;
    double dxa = cx - a->x, dza = cz - a->z, dxb, dzb;
    double ang_a = angle_of(dxa, dza), ra = sqrt(dxa * dxa + dza * dza), ang_b, rb;
    double cn = cos(normal), sn = sin(normal);
    double vax, vaz, vbx, vbz, closing, turn_a, turn_b, ja, jb;
    int side = sign(dza * cn - dxa * sn);

    dxb = cx - b->x;
    dzb = cz - b->z;
    ang_b = angle_of(dxb, dzb);
    rb = sqrt(dxb * dxb + dzb * dzb);
    /* The bodies' velocities at the contact. */
    vax = a->vx - a->yaw_rate * ra * sin(ang_a);
    vaz = a->yaw_rate * ra * cos(ang_a) + a->vz;
    vbx = b->vx - sin(ang_b) * b->yaw_rate * rb;
    vbz = cos(ang_b) * b->yaw_rate * rb + b->vz;
    if (sign((vaz - vbz) * cn - (vax - vbx) * sn) != side) {
        return 0;
    }
    closing = (vbx * sn - vbz * cn) - (sn * vax - cn * vaz);
    a->impact = fabs(closing);
    b->impact = a->impact;
    turn_a = cos(normal - ang_a) / a->inertia * a->mass * ra * 2.0;
    turn_b = cos(normal - ang_b) / b->inertia * b->mass * rb * 2.0;
    ja = -(b->mass / (b->mass + a->mass) * closing * (1.0 + e))
         / ((sn * sin(ang_a) + cos(ang_a) * cn) * ra * turn_a + 1.0);
    jb = -(a->mass / (b->mass + a->mass) * closing * (1.0 + e))
         / ((sin(ang_b) * sn + cos(ang_b) * cn) * turn_b * rb + 1.0);
    a->yaw_rate = turn_a * ja + a->yaw_rate;
    a->vx -= sn * ja;
    a->vz = cn * ja + a->vz;
    a->x = sign(ja) * sn * -0.25 + a->x;
    a->z = sign(ja) * cn * 0.25 + a->z;
    b->vz -= jb * cn;
    b->yaw_rate -= jb * turn_b;
    b->vx = jb * sn + b->vx;
    b->x = sign(jb) * sn * 0.25 + b->x;
    b->z = sign(jb) * cn * -0.25 + b->z;
    return 1;
}

/* The body's outline: its corners in order (0x492ab0). */
static const int s_edges[4][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };

/* 0x40f050
 * Two cars: where their outlines cross, at most 8 points, the mean is
 * the contact and the line through the two points furthest apart its
 * normal; the impulse then pushes them apart. Returns 1 if they
 * overlap. */
int phys_collide(struct car_phys *a, struct car_phys *b)
{
    double px[8], pz[8], dx, dz, r, sx = 0, sz = 0, mx, mz, best = 0, normal;
    int n = 0, i, j, ea, eb, far_i = 0, far_j = 0;

    a->impact = b->impact = 0;
    a->contact_x = a->contact_z = 0;
    b->contact_x = b->contact_z = 0;
    dx = a->x - b->x;
    dz = a->z - b->z;
    r = b->f130 + a->f130;
    if (r * r < dx * dx + dz * dz) {
        return 0;
    }
    for (ea = 0; ea < 4; ea++) {
        double p1x = a->corner_world[2 * s_edges[ea][0]], p1z = a->corner_world[2 * s_edges[ea][0] + 1];
        double p2x = a->corner_world[2 * s_edges[ea][1]], p2z = a->corner_world[2 * s_edges[ea][1] + 1];
        double dxa = p1x - p2x, dza = p1z - p2z;

        for (eb = 0; eb < 4; eb++) {
            double q1x = b->corner_world[2 * s_edges[eb][0]], q1z = b->corner_world[2 * s_edges[eb][0] + 1];
            double q2x = b->corner_world[2 * s_edges[eb][1]], q2z = b->corner_world[2 * s_edges[eb][1] + 1];
            double dxb = q1x - q2x, dzb = q1z - q2z, denom;

            /* Each segment's ends on either side of the other's line. */
            if (sign((p1z - q1z) * dxa - (p1x - q1x) * dza)
                == sign((p1z - q2z) * dxa - (p1x - q2x) * dza)) {
                continue;
            }
            if (sign((q1z - p1z) * dxb - (q1x - p1x) * dzb)
                == sign((q1z - p2z) * dxb - (q1x - p2x) * dzb)) {
                continue;
            }
            denom = dzb * dxa - dza * dxb;
            if (denom == 0.0) {
                continue;
            }
            if (n == 8) {
                return 0;
            }
            px[n] = ((p1z - q1z) * dxa * dxb - p1x * (dza * dxb) + dzb * dxa * q1x) / denom;
            pz[n] = (q1z * (dza * dxb) + (p1x - q1x) * dza * dzb - p1z * (dzb * dxa)) / -denom;
            n++;
        }
    }
    if (n < 1) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        sx += px[i];
        sz += pz[i];
    }
    mx = sx / n;
    mz = sz / n;
    /* The original leaves the pair unset with one point; it is 0, 0
     * here, so the normal is 0. */
    for (i = 0; i <= n - 2; i++) {
        for (j = i; j <= n - 1; j++) {
            double d = (pz[i] - pz[j]) * (pz[i] - pz[j]) + (px[i] - px[j]) * (px[i] - px[j]);

            if (d > best) {
                best = d;
                far_i = i;
                far_j = j;
            }
        }
    }
    normal = angle_of(px[far_i] - px[far_j], pz[far_i] - pz[far_j]);
    a->contact_x = b->contact_x = mx;
    a->contact_z = b->contact_z = mz;
    phys_impulse(a, b, mx, mz, normal);
    phys_corners(a, 1.0);
    phys_corners(b, 1.0);
    return 1;
}

/* 0x40f6f0
 * A body against something that does not move (a wall), touching at
 * (cx, cz) across a normal at angle `normal`. Bounces at 0.3 of the
 * body's bounciness; the turn the hit gives is held to 0.1. Returns 1
 * if the body was moving into it. */
int phys_wall_impulse(struct car_phys *p, double cx, double cz, double normal)
{
    double e = p->f010 * 0.3, dx = cx - p->x, dz = cz - p->z;
    double ang = angle_of(dx, dz), r = sqrt(dx * dx + dz * dz);
    double cn = cos(normal), sn = sin(normal), along, across, closing, turn, j;
    int side = sign(dz * cn - dx * sn);

    along = (p->yaw_rate * r * cos(ang) + p->vz) * cn;
    across = (p->vx - p->yaw_rate * r * sin(ang)) * sn;
    if (sign(along - across) != side) {
        return 0;
    }
    closing = -(across - along);
    p->impact = fabs(closing);
    turn = cos(normal - ang) / p->inertia * p->mass * r * 2.0 * 0.5;
    if (turn > 0.1) {
        turn = 0.1;
    }
    if (turn < -0.1) {
        turn = -0.1;
    }
    j = -((1.0 + e) * closing) / ((sn * sin(ang) + cos(ang) * cn) * turn * r + 1.0);
    p->yaw_rate = j * turn + p->yaw_rate;
    p->vx -= j * sn;
    p->vz = j * cn + p->vz;
    p->x = sign(j) * sn * -0.25 + p->x;
    p->z = sign(j) * cn * 0.25 + p->z;
    return 1;
}

/* 0x40f960
 * A body against walls: `count` segments of four doubles (x1, z1, x2,
 * z2, in world units) 0x20 bytes apart. Where the body's scaled outline
 * crosses them, at most 8 points, the mean is the contact and the line
 * through the two points furthest apart the wall's direction. Needs two
 * points; returns 1 if it hit. */
int phys_walls(struct car_phys *p, const double *segs, int count)
{
    double px[8], pz[8], sx = 0, sz = 0, mx, mz, best = 0, normal;
    int n = 0, s, e, i, j, far_i = 0, far_j = 0;

    for (s = 0; s < count; s++, segs += 4) {
        double x1 = segs[0] * 0.04596, z1 = segs[1] * 0.04596;
        double x2 = segs[2] * 0.04596, z2 = segs[3] * 0.04596;
        double dxw = x1 - x2, dzw = z1 - z2;

        for (e = 0; e < 4; e++) {
            double ax = p->corner_scaled[2 * s_edges[e][0]], az = p->corner_scaled[2 * s_edges[e][0] + 1];
            double bx = p->corner_scaled[2 * s_edges[e][1]], bz = p->corner_scaled[2 * s_edges[e][1] + 1];
            double dxe = ax - bx, dze = az - bz, denom;

            if (sign((z1 - az) * dxw - (x1 - ax) * dzw)
                == sign((z1 - bz) * dxw - (x1 - bx) * dzw)) {
                continue;
            }
            if (sign((az - z1) * dxe - (ax - x1) * dze)
                == sign((az - z2) * dxe - (ax - x2) * dze)) {
                continue;
            }
            denom = dxw * dze - dxe * dzw;
            if (denom == 0.0) {
                continue;
            }
            if (n == 8) {
                return 0;
            }
            px[n] = (ax * (dxw * dze) + (z1 - az) * dxe * dxw - x1 * (dxe * dzw)) / denom;
            pz[n] = ((x1 - ax) * dze * dzw - z1 * (dxw * dze) + az * (dxe * dzw)) / -denom;
            n++;
        }
    }
    if (n <= 1) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        sx += px[i];
        sz += pz[i];
    }
    mx = sx / n;
    mz = sz / n;
    for (i = 0; i <= n - 2; i++) {
        for (j = i; j <= n - 1; j++) {
            double d = (px[i] - px[j]) * (px[i] - px[j]) + (pz[i] - pz[j]) * (pz[i] - pz[j]);

            if (d > best) {
                best = d;
                far_i = i;
                far_j = j;
            }
        }
    }
    normal = angle_of(px[far_i] - px[far_j], pz[far_i] - pz[far_j]);
    phys_wall_impulse(p, mx, mz, normal);
    phys_corners(p, 1.0);
    return 1;
}
