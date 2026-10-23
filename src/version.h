/*
 * version.h - this port's version, apart from the original game's
 * (IGNITION_VERSION in startup.c, 6.53).
 *
 * The number lives in the VERSION file at the top of the repository;
 * the build passes it as IGN_VERSION. CI passes the release tag
 * instead, or vX.Y.Z-edge for a build of main.
 */

#ifndef IGN_VERSION_H
#define IGN_VERSION_H

#ifndef IGN_VERSION
#define IGN_VERSION "dev"
#endif

#endif /* IGN_VERSION_H */
