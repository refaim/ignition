# Ignition on iOS and iPadOS

The iOS host is `src/platform/ios.m`, on Apple's frameworks only: UIKit,
OpenGL ES 3.0 on a `CAEAGLLayer` (which iOS still has but marks as
deprecated), touch, GameController pads and keyboards, and a RemoteIO
audio unit. The game runs on its own thread; UIKit keeps the main one.
It needs iOS 15 or later.

## Build

You need Xcode and CMake.

```sh
make ios-sim     # build-ios/Release-iphonesimulator/Ignition.app
make ios         # build-ios/ignition.ipa, unsigned
make ios-clean
```

CMake's Xcode generator makes the project in `build-ios/`. Open
`build-ios/ignition_ios.xcodeproj` in Xcode to run the app on a device.

### Signing

`make ios` makes an unsigned IPA. Sideloading tools such as AltStore or
Sideloadly sign it with your own Apple ID when they install it. To sign
in Xcode, configure with your team:

```sh
cmake -S platforms/ios -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
      -DDEVELOPMENT_TEAM=<team id> -DBUNDLE_ID=<your bundle id>
```

## Game data

The app's Documents folder shows in the Files app, under **On My iPhone**
(or iPad) → **Ignition**, and in the Finder when the device is connected
to a Mac. Copy the disc image of your own copy of the game (the `.cue`
file and its `.bin` files) into it. At the first start, the game copies
the files out of the image into a `disc/` folder there. A `disc/` folder
with the CD's files works too.

## Controls

Touch the screen once, and an on-screen pad shows: a d-pad on the left,
A, B, X and Y on the right, Select and Start at the top. The buttons do
what a gamepad's do (see [../linux/README.md](../linux/README.md#controls)).
Game controllers and hardware keyboards also work.
