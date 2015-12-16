/** @file
    @brief Produces a distortion mesh and partial display description
           from a table of display locations to angle inputs.

    @date 2015

    @author
    Russ Taylor working through ReliaSolve.com for Sensics, Inc.
    <http://sensics.com/osvr>
*/

// Copyright 2015 Sensics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Internal Includes
#include <json/json.h>

// Standard includes
#include <string>
#include <iostream>
#include <cmath>
#include <stdlib.h> // For exit()

#define MY_PI (4.0*atan(1.0))

// Screen-space to/from angle-space map entry
typedef struct {
  double x;
  double y;
  double latitude;
  double longitude;
} XYLatLong;

// 3D coordinate
class XYZ {
public:
  double x;
  double y;
  double z;

  /// Return the rotation about the Y axis, where 0 rotation points along
  // the -Z axis and positive rotation heads towards the -X axis.
  double rotationAboutY() const {
    return atan2(-x, -z);
  }

  /// Project from the origin through our point onto a plane whose
  // equation is specified.
  XYZ projectOntoPlane(double A, double B, double C, double D) const {
    XYZ ret;

    // Solve for the value of S that satisfies:
    //    Asx + Bsy + Csz + D = 0,
    //    s = -D / (Ax + By + Cz)
    // Then for the location sx, sy, sz.

    double s = -D / (A*x + B*y + C*z);
    ret.x = s*x;
    ret.y = s*y;
    ret.z = s*z;
    return ret;
  }

  /// Return the rotation distance from another point.
  double distanceFrom(const XYZ &p) const {
    return sqrt( (x-p.x)*(x-p.x) + (y-p.y)*(y-p.y) + (z-p.z)*(z-p.z) );
  }
};

// Mapping entry, along with its associated 3D coordinate
typedef struct {
  XYLatLong xyLatLong;
  XYZ xyz;
} Mapping;

// Description of a screen
typedef struct {
  double hFOVDegrees;
  double vFOVDegrees;
  double overlapPercent;
  double xCOP;
  double yCOP;
} ScreenDescription;

bool findScreen(const std::vector<Mapping> &mapping, ScreenDescription &ret)
{
  //====================================================================
  // Figure out the X screen-space extents.
  // The X screen-space extents are defined by the lines perpendicular to the
  // Y axis passing through:
  //  left: the point location whose reprojection into the Y = 0 plane has the most -
  //        positive angle(note that this may not be the point with the largest
  //        longitudinal coordinate, because of the impact of changing latitude on
  //        X - Z position).
  //  right : the point location whose reprojection into the Y = 0 plane has the most -
  //        negative angle(note that this may not be the point with the smallest
  //        longitudinal coordinate, because of the impact of changing latitude on
  //        X - Z position).
  XYZ screenLeft, screenRight;
  screenLeft = screenRight = mapping[0].xyz;
  for (size_t i = 0; i < mapping.size(); i++) {
    if (mapping[i].xyz.rotationAboutY() > screenLeft.rotationAboutY()) {
      screenLeft = mapping[i].xyz;
    }
    if (mapping[i].xyz.rotationAboutY() < screenRight.rotationAboutY()) {
      screenRight = mapping[i].xyz;
    }
  }
  if (screenLeft.rotationAboutY() - screenRight.rotationAboutY() >= MY_PI) {
    std::cerr << "Error: Field of view > 180 degrees: found " <<
      180 / MY_PI * (screenLeft.rotationAboutY() - screenRight.rotationAboutY())
      << std::endl;
    return false;
  }

  //====================================================================
  // Find the plane of the screen, using the equation that has the normal
  // pointing towards the origin.  This is AX + BY + CZ + D = 0, where the
  // normal is in A, B, C and the offset is in D.
  //   Two points on the plane are given above.  Two more are the projection
  // of each of these points into the Y=0 plane.  We take the cross
  // product of the line from the left-most projected point to the right-
  // most projected point with the vertical line to get the normal to that
  // plane that points towards the origin.  Then we normalize this and plug
  // it back into the equation to solve for D.
  //   We're crossing with the vector (0, 1, 0), so we get:
  //   x = -dz
  //   y = 0
  //   z = dx
  double dx = screenRight.x - screenLeft.x;
  double dz = screenRight.z - screenLeft.z;
  double A = -dz;
  double B = 0;
  double C = dx;
  double len = sqrt(A*A + B*B + C*C);
  A /= len;
  B /= len;
  C /= len;
  double D = -(A*screenRight.x + B*screenRight.y + C*screenRight.z);

  //====================================================================
  // Figure out the Y screen-space extents.
  // The Y screen-space extents are symmetric and correspond to the lines parallel
  //  to the screen X axis that are within the plane of the X line specifying the
  //  axis extents at the largest magnitude angle up or down from the horizontal.
  // Find the highest-magnitude Y value of all points when they are
  // projected into the plane of the screen.
  double maxY = fabs(mapping[0].xyz.projectOntoPlane(A, B, C, D).y);
  for (size_t i = 1; i < mapping.size(); i++) {
    double Y = fabs(mapping[i].xyz.projectOntoPlane(A, B, C, D).y);
    if (Y > maxY) { maxY = Y; }
  }

  //====================================================================
  // Figure out the monocular horizontal field of view for the screen.
  // Find the distance between the left and right points projected
  // into the Y=0 plane.  The FOV is twice the arctangent of half of this
  // distance divided by the distance to the screen.
  XYZ leftProj = screenLeft;
  XYZ rightProj = screenRight;
  leftProj.y = 0;
  rightProj.y = 0;
  double screenWidth = leftProj.distanceFrom(rightProj);
  double hFOVRadians = 2 * atan((screenWidth / 2) / fabs(D));
  double hFOVDegrees = hFOVRadians * 180 / MY_PI;

  //====================================================================
  // Figure out the monocular vertical field of view for the screen.
  // The FOV is twice the arctangent of half of the Y
  // distance divided by the distance to the screen.
  double vFOVRadians = 2 * atan(maxY / fabs(D));
  double vFOVDegrees = vFOVRadians * 180 / MY_PI;

  //====================================================================
  // Figure out the overlap percent for the screen that corresponds to
  // the angle between straight ahead and the normal to the plane.  First
  // find the angle itself, and then the associated overlap percent.
  // The angle is determined based on the unit normal to the plane,
  // which is (A,B,C), but B = 0 and we only care about rotation
  // around the Y axis.  For the purpose of the atan function, the part
  // of X is played by the -Z axis and the part of Y is played by the
  // -X axis.  A is associated with the X axis and C with the Z axis.
  // Here is the code we are inverting...
  //  double overlapFrac = m_params.m_displayConfiguration.getOverlapPercent();
  //  const auto hfov = m_params.m_displayConfiguration.getHorizontalFOV();
  //  const auto angularOverlap = hfov * overlapFrac;
  //  rotateEyesApart = (hfov - angularOverlap) / 2.;
  // Here is the inversion:
  //  rotateEyesApart = (hfov - (hfov * overlapFrac));
  //  rotateEyesApart - hfov = - hfov * overlapFrac;
  //  1 - rotateEyesApart/hfov = overlapFrac
  double angleRadians = fabs(atan2(-A, -C));
  double overlapFrac = 1 - angleRadians / hFOVRadians;
  double overlapPercent = overlapFrac * 100;

  //====================================================================
  // Figure out the center of projection for the screen.  This is the
  // location where a line from the origin perpendicular to the screen
  // pierces the screen.
  // Then figure out the normalized coordinates of this point in screen
  // space, which is the fraction of the way from the left to the right
  // of the screen.  It is always (by construction above) in the center
  // of the screen in Y.
  // Also, by construction it is at a distance D along the (A,B,C) unit
  // vector from the origin.
  double yCOP = 0.5;
  XYZ projection;
  projection.x = -D * A;
  projection.y = -D * B;
  projection.z = -D * C;
  double xCOP = leftProj.distanceFrom(projection) / leftProj.distanceFrom(rightProj);

  ScreenDescription ret;
  ret.hFOVDegrees = hFOVDegrees;
  ret.vFOVDegrees = vFOVDegrees;
  ret.overlapPercent = overlapPercent;
  ret.xCOP = xCOP;
  ret.yCOP = yCOP;
  return true;
}

void Usage(std::string name)
{
  std::cerr << "Usage: " << name
    << " [-eye right|left] (default is right)"
    << " [-depth_meters D] (default is 2.0)"
    << " [-mm] (default is meters)"
    << " screen_left_meters screen_bottom_meters screen_right_meters screen_top_meters"
    << std::endl
    << "  This program reads from standard input a configuration that has a list of" << std::endl
    << "x,y screen coordinates in meters followed by long,lat angles in" << std::endl
    << "degrees where (0,0) is straight ahead from the eye, positive" << std::endl
    << "longitude is left and positive latitude is up." << std::endl
    << "  It produces on standard output a partial OSVR display configuration file." << std::endl
    << std::endl;
  exit(1);
}

int main(int argc, char *argv[])
{
  // Parse the command line
  bool rightEye = true;
  double left, right, bottom, top;
  double depth = 2.0;
  double toMeters = 1.0;
  int realParams = 0;
  for (int i = 1; i < argc; i++) {
    if (std::string("-mm") == argv[i]) {
      toMeters = 1e-3;  // Convert input in millimeters to meters
    } else if (std::string("-depth_meters") == argv[i]) {
      if (++i >= argc) { Usage(argv[0]); }
      depth = atof(argv[i]);
    } else if (std::string("-eye") == argv[i]) {
      if (++i >= argc) { Usage(argv[0]); }
      std::string eye = argv[i];
      if (eye == "left") {
        rightEye = false;
      } else if (eye == "right") {
        rightEye = true;
      } else {
        std::cerr << "Bad value for -eye: " << eye << ", expected left or right" << std::endl;
        Usage(argv[0]);
      }
    } else if ((argv[i][0] == '-') && (atof(argv[i]) == 0.0) ) {
      Usage(argv[0]);
    }
    else switch (++realParams) {
    case 1:
      left = atof(argv[i]);
      break;
    case 2:
      bottom = atof(argv[i]);
      break;
    case 3:
      right = atof(argv[i]);
      break;
    case 4:
      top = atof(argv[i]);
      break;
    default:
      Usage(argv[0]);
    }
  }
  if (realParams != 4) { Usage(argv[0]); }

  //====================================================================
  // Parse the angle-configuration information from standard input.  Expect white-space
  // separation between numbers and also between entries (which may be on separate
  // lines).
  std::vector<Mapping> mapping;
  while (!std::cin.eof()) {
    // Read the mapping info from the input file.
    Mapping map;
    std::cin >> map.xyLatLong.x >> map.xyLatLong.y >> map.xyLatLong.latitude >> map.xyLatLong.longitude;

    //  Convert the input coordinates from its input space into meters
    // and then convert (using the screen dimensions) into normalized screen units.
    map.xyLatLong.x *= toMeters;
    map.xyLatLong.x = (map.xyLatLong.x - left) / (right - left);
    map.xyLatLong.y *= toMeters;
    map.xyLatLong.y = (map.xyLatLong.y - bottom) / (top - bottom);

    // Convert the input latitude and longitude from degrees to radians.
    map.xyLatLong.latitude *= MY_PI / 180;
    map.xyLatLong.longitude *= MY_PI / 180;

    // Compute the 3D coordinate of the point w.r.t. the eye at the origin.
    // longitude = 0, lattitude = 0 points along the -Z axis in eye space.
    map.xyz.y = depth * sin(map.xyLatLong.latitude);
    map.xyz.z = -depth * cos(map.xyLatLong.longitude) * cos(map.xyLatLong.latitude);
    map.xyz.x = -depth * sin(map.xyLatLong.longitude) * cos(map.xyLatLong.latitude);

    mapping.push_back(map);
  }
  if (mapping.size() == 0) {
    std::cerr << "Error: No input points found" << std::endl;
    return 2;
  }

  //====================================================================
  // Determine the screen description based on the input points.
  ScreenDescription screen;
  if (!findScreen(mapping, screen)) {
    std::cerr << "Error: Could not find screen" << std::endl;
    return 3;
  }

  //====================================================================
  // Determine the distortion mesh based on the screen and the input
  // points.
  // @todo

  //====================================================================
  // Construct Json screen description.
  Json::Value jRoot;
  Json::Value jDisplay;
  Json::Value jHmd;
  Json::Value jFOV;
  Json::Value jHFOV = screen.hFOVDegrees;
  Json::Value jVFOV = screen.vFOVDegrees;
  Json::Value jOverlap = screen.overlapPercent;
  Json::Value jPitch = 0.0;
  jFOV["monocular_horizontal"] = jHFOV;
  jFOV["monocular_vertical"] = jVFOV;
  jFOV["overlap_percent"] = jOverlap;
  jFOV["pitch_tilt"] = jPitch;
  jHmd["field_of_view"] = jFOV;
  jDisplay["hmd"] = jHmd;
  jRoot["display"] = jDisplay;

  //====================================================================
  // Construct the Json distortion mesh description and add it to
  // the existing HMD description.
  // @todo

  //====================================================================
  // Write the complete description.
  Json::FastWriter jWriter;
  std::cout << jWriter.write(jRoot) << std::endl;

  return 0;
}

