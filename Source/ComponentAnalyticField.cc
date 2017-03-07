#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>

#include "Numerics.hh"
#include "GarfieldConstants.hh"
#include "ComponentAnalyticField.hh"

namespace Garfield {

ComponentAnalyticField::ComponentAnalyticField() {

  m_className = "ComponentAnalyticField";
  m_chargeCheck = false;
  CellInit();
}

void ComponentAnalyticField::ElectricField(const double x, const double y,
                                           const double z, double& ex,
                                           double& ey, double& ez, Medium*& m,
                                           int& status) {

  // Initialize electric field and medium.
  ex = ey = ez = 0.;
  m = NULL;

  // Make sure the charges have been calculated.
  if (!m_cellset) {
    if (!Prepare()) {
      status = -11;
      return;
    }
  }

  // Disable calculation of the potential.
  const bool opt = false;
  double v = 0.;

  // Calculate the field.
  status = Field(x, y, z, ex, ey, ez, v, opt);

  // If the field is ok, get the medium.
  if (status == 0) {
    m = GetMedium(x, y, z);
    if (!m) {
      status = -6;
    } else if (!m->IsDriftable()) {
      status = -5;
    }
  }
}

void ComponentAnalyticField::ElectricField(const double x, const double y,
                                           const double z, double& ex,
                                           double& ey, double& ez, double& v,
                                           Medium*& m, int& status) {

  // Initialize electric field and medium.
  ex = ey = ez = v = 0.;
  m = NULL;

  // Make sure the charges have been calculated.
  if (!m_cellset) {
    if (!Prepare()) {
      status = -11;
      return;
    }
  }

  // Request calculation of the potential.
  const bool opt = true;

  // Calculate the field.
  status = Field(x, y, z, ex, ey, ez, v, opt);

  // If the field is ok, get the medium.
  if (status == 0) {
    m = GetMedium(x, y, z);
    if (!m) {
      status = -6;
    } else if (!m->IsDriftable()) {
      status = -5;
    }
  }
}

bool ComponentAnalyticField::GetVoltageRange(double& pmin, double& pmax) {

  // Make sure the cell is prepared.
  if (!m_cellset) {
    if (!Prepare()) {
      std::cerr << m_className << "::GetVoltageRange:\n";
      std::cerr << "    Unable to return voltage range.\n";
      std::cerr << "   Cell could not be setup.\n";
      return false;
    }
  }

  pmin = vmin;
  pmax = vmax;
  return true;
}

void ComponentAnalyticField::WeightingField(const double x, const double y,
                                            const double z, double& wx,
                                            double& wy, double& wz,
                                            const std::string& label) {

  wx = wy = wz = 0.;
  // Stop here if there are no weighting fields defined.
  if (m_readout.empty()) return;
  // Prepare the weighting fields.
  if (!m_sigset) {
    if (!PrepareSignals()) {
      std::cerr << m_className << "::WeightingField::\n";
      std::cerr << "    Unable to calculate weighting fields.\n";
      return;
    }
  }

  if (label.empty()) return;
  std::vector<std::string>::iterator it =
      std::find(m_readout.begin(), m_readout.end(), label);
  if (it == m_readout.end()) return;
  const int index = it - m_readout.begin();

  double volt = 0.;
  Wfield(x, y, z, wx, wy, wz, volt, index, false);
}

double ComponentAnalyticField::WeightingPotential(const double x,
                                                  const double y,
                                                  const double z,
                                                  const std::string& label) {
  double volt = 0.;

  if (m_readout.empty()) return volt;
  if (!m_sigset) {
    if (!PrepareSignals()) {
      std::cerr << m_className << "::WeightingPotential::\n";
      std::cerr << "    Unable to calculate weighting fields.\n";
      return volt;
    }
  }

  if (label.empty()) return volt;
  std::vector<std::string>::iterator it =
      std::find(m_readout.begin(), m_readout.end(), label);
  if (it == m_readout.end()) return volt;
  const int index = it - m_readout.begin();

  double wx = 0., wy = 0., wz = 0.;
  Wfield(x, y, z, wx, wy, wz, volt, index, true);
  return volt;
}

bool ComponentAnalyticField::GetBoundingBox(double& x0, double& y0, double& z0,
                                            double& x1, double& y1,
                                            double& z1) {

  // If a geometry is present, try to get the bounding box from there.
  if (m_geometry) {
    if (m_geometry->GetBoundingBox(x0, y0, z0, x1, y1, z1)) return true;
  }
  // Otherwise, return the cell dimensions.
  if (!m_cellset) return false;
  x0 = xmin;
  y0 = ymin;
  z0 = zmin;
  x1 = xmax;
  y1 = ymax;
  z1 = zmax;
  return true;
}

bool ComponentAnalyticField::IsWireCrossed(const double x0, const double y0, 
                                           const double z0,
                                           const double x1, const double y1, 
                                           const double z1,
                                           double& xc, double& yc, double& zc) {

  xc = x0;
  yc = y0;
  zc = z0;

  if (m_w.empty()) return false;

  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double d2 = dx * dx + dy * dy;
  // Check that the step length is non-zero.
  if (d2 < Small) return false;

  // Check if a whole period has been crossed.
  if ((m_perx && fabs(dx) >= m_sx) || (m_pery && fabs(dy) >= m_sy)) {
    std::cerr << m_className << "::IsWireCrossed:\n";
    std::cerr << "    Particle crossed more than one period.\n";
    return false;
  }

  // Both coordinates are assumed to be located inside
  // the drift area and inside a drift medium.
  // This should have been checked before this call.

  const double xm = 0.5 * (x0 + x1);
  const double ym = 0.5 * (y0 + y1);
  double dMin2 = 0.;
  for (int i = m_nWires; i--;) {
    double xw = m_w[i].x, yw = m_w[i].y;
    if (m_perx) {
      xw += m_sx * int(round((xm - xw) / m_sx));
    }
    if (m_pery) {
      yw += m_sy * int(round((ym - yw) / m_sy));
    }
    // Calculate the smallest distance between track and wire.
    const double xIn0 = dx * (xw - x0) + dy * (yw - y0);
    // Check if the minimum is located before (x0, y0).
    if (xIn0 < 0.) continue;
    const double xIn1 = -(dx * (xw - x1) + dy * (yw - y1));
    // Check if the minimum is located behind (x1, y1).
    if (xIn1 < 0.) continue;
    // Minimum is located between (x0, y0) and (x1, y1).
    const double xw0 = xw - x0;
    const double xw1 = xw - x1;
    const double yw0 = yw - y0;
    const double yw1 = yw - y1;
    const double dw02 = xw0 * xw0 + yw0 * yw0;
    const double dw12 = xw1 * xw1 + yw1 * yw1;
    if (xIn1 * xIn1 * dw02 > xIn0 * xIn0 * dw12) {
      dMin2 = dw02 - xIn0 * xIn0 / d2;
    } else {
      dMin2 = dw12 - xIn1 * xIn1 / d2;
    }
    // Add in the times nTrap to account for the trap radius.
    const double r2 = 0.25 * m_w[i].d * m_w[i].d;
    if (dMin2 < r2) {
      // Wire has been crossed.
      // Find the point of intersection.
      const double p = -xIn0 / d2;
      const double q = (dw02 - r2) / d2;
      const double t1 = -p + sqrt(p * p - q);
      const double t2 = -p - sqrt(p * p - q);
      const double t = std::min(t1, t2);
      xc = x0 + t * dx;
      yc = y0 + t * dy;
      zc = z0 + t * (z1 - z0);
      return true;
    }
  }
  return false;
}

bool ComponentAnalyticField::IsInTrapRadius(const double qin, const double xin,
                                            const double yin, const double zin,
                                            double& xw, double& yw,
                                            double& rw) {

  // In case of periodicity, move the point into the basic cell.
  double x0 = xin;
  double y0 = yin;
  int nX = 0, nY = 0, nPhi = 0;
  if (m_perx) {
    nX = int(round(xin / m_sx));
    x0 -= m_sx * nX;
  }
  if (m_pery && m_tube) {
    Cartesian2Polar(xin, yin, x0, y0);
    nPhi = int(round((Pi * y0) / (m_sy * 180.)));
    y0 -= 180. * m_sy * nPhi / Pi;
    Polar2Cartesian(x0, y0, x0, y0);
  } else if (m_pery) {
    nY = int(round(yin / m_sy));
    y0 -= m_sy * nY;
  }

  // Move the point to the correct side of the plane.
  if (m_perx && m_ynplan[0] && x0 <= m_coplan[0]) x0 += m_sx;
  if (m_perx && m_ynplan[1] && x0 >= m_coplan[1]) x0 -= m_sx;
  if (m_pery && m_ynplan[2] && y0 <= m_coplan[2]) y0 += m_sy;
  if (m_pery && m_ynplan[3] && y0 >= m_coplan[3]) y0 -= m_sy;

  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Skip wires with the wrong charge.
    if (qin * m_w[i].e > 0.) continue;
    const double dxw0 = m_w[i].x - x0;
    const double dyw0 = m_w[i].y - y0;
    const double r2 = dxw0 * dxw0 + dyw0 * dyw0;
    const double rTrap = 0.5 * m_w[i].d * m_w[i].nTrap;
    if (r2 < rTrap * rTrap) {
      xw = m_w[i].x;
      yw = m_w[i].y;
      rw = m_w[i].d * 0.5;
      if (m_perx && m_ynplan[0] && x0 <= m_coplan[0]) x0 -= m_sx;
      if (m_perx && m_ynplan[1] && x0 >= m_coplan[1]) x0 += m_sx;
      if (m_pery && m_ynplan[2] && y0 <= m_coplan[2]) y0 -= m_sy;
      if (m_pery && m_ynplan[3] && y0 >= m_coplan[3]) y0 += m_sy;
      if (m_pery && m_tube) {
        double rhow, phiw;
        Cartesian2Polar(xw, yw, rhow, phiw);
        phiw += 180. * m_sy * nPhi / Pi;
        Polar2Cartesian(rhow, phiw, xw, yw);
      } else if (m_pery) {
        y0 += m_sy * nY;
      }
      if (m_perx) xw += m_sx * nX;
      if (m_debug) {
        std::cout << m_className << "::IsInTrapRadius:\n";
        std::cout << "    (" << xin << ", " << yin << ", " << zin << ")"
                  << " within trap radius of wire " << i << ".\n";
      }
      return true;
    }
  }

  return false;
}

void ComponentAnalyticField::AddWire(const double x, const double y,
                                     const double diameter,
                                     const double voltage,
                                     const std::string& label,
                                     const double length, const double tension,
                                     double rho, const int ntrap) {

  // Check if the provided parameters make sense.
  if (diameter <= 0.) {
    std::cerr << m_className << "::AddWire:\n";
    std::cerr << "    Unphysical wire diameter.\n";
    return;
  }

  if (tension <= 0.) {
    std::cerr << m_className << "::AddWire:\n";
    std::cerr << "    Unphysical wire tension.\n";
    return;
  }

  if (rho <= 0.0) {
    std::cerr << m_className << "::AddWire:\n";
    std::cerr << "    Unphysical wire density.\n";
    return;
  }

  if (length <= 0.0) {
    std::cerr << m_className << "::AddWire:\n";
    std::cerr << "    Unphysical wire length.\n";
    return;
  }

  if (ntrap <= 0) {
    std::cerr << m_className << "::AddWire:\n";
    std::cerr << "    Number of trap radii must be > 0.\n";
    return;
  }
  // Create a new wire
  wire newWire;
  newWire.x = x;
  newWire.y = y;
  newWire.d = diameter;
  newWire.v = voltage;
  newWire.u = length;
  newWire.type = label;
  newWire.e = 0.;
  newWire.ind = -1;
  newWire.nTrap = ntrap;
  // Add the wire to the list
  m_w.push_back(newWire);
  ++m_nWires;

  // Force recalculation of the capacitance and signal matrices.
  m_cellset = false;
  m_sigset = false;
}

void ComponentAnalyticField::AddTube(const double radius, const double voltage,
                                     const int nEdges,
                                     const std::string& label) {

  // Check if the provided parameters make sense.
  if (radius <= 0.0) {
    std::cerr << m_className << "::AddTube:\n"
              << "    Unphysical tube dimension.\n";
    return;
  }

  if (nEdges < 3 && nEdges != 0) {
    std::cerr << m_className << "::AddTube:\n"
              << "    Unphysical number of tube edges (" << nEdges << ")\n";
    return;
  }

  // If there is already a tube defined, print a warning message.
  if (m_tube) {
    std::cout << m_className << "::AddTube:\n"
              << "    Warning: Existing tube settings will be overwritten.\n";
  }

  // Set the coordinate system.
  m_tube = true;
  m_polar = false;

  // Set the tube parameters.
  m_cotube = radius;
  m_vttube = voltage;

  m_ntube = nEdges;

  planes[4].type = label;
  planes[4].ind = -1;

  // Force recalculation of the capacitance and signal matrices.
  m_cellset = false;
  m_sigset = false;
}

void ComponentAnalyticField::AddPlaneX(const double x, const double v,
                                       const std::string& lab) {

  if (m_ynplan[0] && m_ynplan[1]) {
    std::cerr << m_className << "::AddPlaneX:\n";
    std::cerr << "    There are already two x planes defined.\n";
    return;
  }

  if (m_ynplan[0]) {
    m_ynplan[1] = true;
    m_coplan[1] = x;
    m_vtplan[1] = v;
    planes[1].type = lab;
    planes[1].ind = -1;
  } else {
    m_ynplan[0] = true;
    m_coplan[0] = x;
    m_vtplan[0] = v;
    planes[0].type = lab;
    planes[0].ind = -1;
  }

  // Force recalculation of the capacitance and signal matrices.
  m_cellset = false;
  m_sigset = false;
}

void ComponentAnalyticField::AddPlaneY(const double y, const double v,
                                       const std::string& lab) {

  if (m_ynplan[2] && m_ynplan[3]) {
    std::cerr << m_className << "::AddPlaneY:\n";
    std::cerr << "    There are already two y planes defined.\n";
    return;
  }

  if (m_ynplan[2]) {
    m_ynplan[3] = true;
    m_coplan[3] = y;
    m_vtplan[3] = v;
    planes[3].type = lab;
    planes[3].ind = -1;
  } else {
    m_ynplan[2] = true;
    m_coplan[2] = y;
    m_vtplan[2] = v;
    planes[2].type = lab;
    planes[2].ind = -1;
  }

  // Force recalculation of the capacitance and signal matrices.
  m_cellset = false;
  m_sigset = false;
}

void ComponentAnalyticField::AddStripOnPlaneX(const char direction,
                                              const double x, const double smin,
                                              const double smax,
                                              const std::string& label,
                                              const double gap) {

  if (!m_ynplan[0] && !m_ynplan[1]) {
    std::cerr << m_className << "::AddStripOnPlaneX:\n";
    std::cerr << "    There are no planes at constant x defined.\n";
    return;
  }

  if (direction != 'y' && direction != 'Y' && direction != 'z' &&
      direction != 'Z') {
    std::cerr << m_className << "::AddStripOnPlaneX:\n";
    std::cerr << "    Invalid direction (" << direction << ").\n";
    std::cerr << "    Only strips in y or z direction are possible.\n";
    return;
  }

  if (fabs(smax - smin) < Small) {
    std::cerr << m_className << "::AddStripOnPlaneX:\n";
    std::cerr << "    Strip width must be greater than zero.\n";
    return;
  }

  strip newStrip;
  newStrip.type = label;
  newStrip.ind = -1;
  newStrip.smin = std::min(smin, smax);
  newStrip.smax = std::max(smin, smax);
  if (gap > Small) {
    newStrip.gap = gap;
  } else {
    newStrip.gap = -1.;
  }

  int iplane = 0;
  if (m_ynplan[1]) {
    const double d0 = fabs(m_coplan[0] - x);
    const double d1 = fabs(m_coplan[1] - x);
    if (d1 < d0) iplane = 1;
  }

  if (direction == 'y' || direction == 'Y') {
    planes[iplane].strips1.push_back(newStrip);
  } else {
    planes[iplane].strips2.push_back(newStrip);
  }
}

void ComponentAnalyticField::AddStripOnPlaneY(const char direction,
                                              const double y, const double smin,
                                              const double smax,
                                              const std::string& label,
                                              const double gap) {

  if (!m_ynplan[2] && !m_ynplan[3]) {
    std::cerr << m_className << "::AddStripOnPlaneY:\n";
    std::cerr << "    There are no planes at constant y defined.\n";
    return;
  }

  if (direction != 'x' && direction != 'X' && direction != 'z' &&
      direction != 'Z') {
    std::cerr << m_className << "::AddStripOnPlaneY:\n";
    std::cerr << "    Invalid direction (" << direction << ").\n";
    std::cerr << "    Only strips in x or z direction are possible.\n";
    return;
  }

  if (fabs(smax - smin) < Small) {
    std::cerr << m_className << "::AddStripOnPlaneY:\n";
    std::cerr << "    Strip width must be greater than zero.\n";
    return;
  }

  strip newStrip;
  newStrip.type = label;
  newStrip.ind = -1;
  newStrip.smin = std::min(smin, smax);
  newStrip.smax = std::max(smin, smax);
  if (gap > Small) {
    newStrip.gap = gap;
  } else {
    newStrip.gap = -1.;
  }

  int iplane = 2;
  if (m_ynplan[3]) {
    const double d2 = fabs(m_coplan[2] - y);
    const double d3 = fabs(m_coplan[3] - y);
    if (d3 < d2) iplane = 3;
  }

  if (direction == 'x' || direction == 'X') {
    planes[iplane].strips1.push_back(newStrip);
  } else {
    planes[iplane].strips2.push_back(newStrip);
  }
}

void ComponentAnalyticField::AddPixelOnPlaneX(
    const double x, const double ymin, const double ymax, const double zmin,
    const double zmax, const std::string& label, const double gap) {

  if (!m_ynplan[0] && !m_ynplan[1]) {
    std::cerr << m_className << "::AddPixelOnPlaneX:\n";
    std::cerr << "    There are no planes at constant x defined.\n";
    return;
  }

  if (fabs(ymax - ymin) < Small || fabs(zmax - zmin) < Small) {
    std::cerr << m_className << "::AddSPixelOnPlaneX:\n";
    std::cerr << "    Pixel width must be greater than zero.\n";
    return;
  }

  pixel newPixel;
  newPixel.type = label;
  newPixel.ind = -1;
  newPixel.smin = std::min(ymin, ymax);
  newPixel.smax = std::max(ymin, ymax);
  newPixel.zmin = std::min(zmin, zmax);
  newPixel.zmax = std::max(zmin, zmax);
  if (gap > Small) {
    newPixel.gap = gap;
  } else {
    newPixel.gap = -1.;
  }

  int iplane = 0;
  if (m_ynplan[1]) {
    const double d0 = fabs(m_coplan[0] - x);
    const double d1 = fabs(m_coplan[1] - x);
    if (d1 < d0) iplane = 1;
  }

  planes[iplane].pixels.push_back(newPixel);
}

void ComponentAnalyticField::AddPixelOnPlaneY(
    const double y, const double xmin, const double xmax, const double zmin,
    const double zmax, const std::string& label, const double gap) {

  if (!m_ynplan[2] && !m_ynplan[3]) {
    std::cerr << m_className << "::AddPixelOnPlaneY:\n";
    std::cerr << "    There are no planes at constant y defined.\n";
    return;
  }

  if (fabs(xmax - xmin) < Small || fabs(zmax - zmin) < Small) {
    std::cerr << m_className << "::AddPixelOnPlaneY:\n";
    std::cerr << "    Pixel width must be greater than zero.\n";
    return;
  }

  pixel newPixel;
  newPixel.type = label;
  newPixel.ind = -1;
  newPixel.smin = std::min(xmin, xmax);
  newPixel.smax = std::max(xmin, xmax);
  newPixel.zmin = std::min(zmin, zmax);
  newPixel.zmax = std::max(zmin, zmax);
  if (gap > Small) {
    newPixel.gap = gap;
  } else {
    newPixel.gap = -1.;
  }

  int iplane = 2;
  if (m_ynplan[3]) {
    const double d0 = fabs(m_coplan[2] - y);
    const double d1 = fabs(m_coplan[3] - y);
    if (d1 < d0) iplane = 3;
  }

  planes[iplane].pixels.push_back(newPixel);
}

void ComponentAnalyticField::SetPeriodicityX(const double s) {

  if (s < Small) {
    std::cerr << m_className << "::SetPeriodicityX:\n";
    std::cerr << "    Periodic length must be greater than zero.\n";
    return;
  }

  m_xPeriodic = true;
  m_sx = s;
  UpdatePeriodicity();
}

void ComponentAnalyticField::SetPeriodicityY(const double s) {

  if (s < Small) {
    std::cerr << m_className << "::SetPeriodicityY:\n";
    std::cerr << "    Periodic length must be greater than zero.\n";
    return;
  }

  m_yPeriodic = true;
  m_sy = s;
  UpdatePeriodicity();
}

bool ComponentAnalyticField::GetPeriodicityX(double& s) {

  if (!m_xPeriodic) {
    s = 0.;
    return false;
  }

  s = m_sx;
  return true;
}

bool ComponentAnalyticField::GetPeriodicityY(double& s) {

  if (!m_yPeriodic) {
    s = 0.;
    return false;
  }

  s = m_sy;
  return true;
}

void ComponentAnalyticField::UpdatePeriodicity() {

  // Check if the settings have actually changed.
  if (m_perx && !m_xPeriodic) {
    m_perx = false;
    m_cellset = false;
    m_sigset = false;
  } else if (!m_perx && m_xPeriodic) {
    if (m_sx < Small) {
      std::cerr << m_className << "::UpdatePeriodicity:\n";
      std::cerr << "    Periodicity in x direction was enabled"
                << " but periodic length is not set.\n";
    } else {
      m_perx = true;
      m_cellset = false;
      m_sigset = false;
    }
  }

  if (m_pery && !m_yPeriodic) {
    m_pery = false;
    m_cellset = false;
    m_sigset = false;
  } else if (!m_pery && m_yPeriodic) {
    if (m_sy < Small) {
      std::cerr << m_className << "::UpdatePeriodicity:\n";
      std::cerr << "    Periodicity in y direction was enabled"
                << " but periodic length is not set.\n";
    } else {
      m_pery = true;
      m_cellset = false;
      m_sigset = false;
    }
  }

  // Check if symmetries other than x/y periodicity have been requested
  if (m_zPeriodic) {
    std::cerr << m_className << "::UpdatePeriodicity:\n";
    std::cerr << "    Periodicity in z is not possible.\n";
  }

  if (m_xMirrorPeriodic || m_yMirrorPeriodic || m_zMirrorPeriodic) {
    std::cerr << m_className << "::UpdatePeriodicity:\n";
    std::cerr << "    Mirror periodicity is not possible.\n";
  }

  if (m_xAxiallyPeriodic || m_yAxiallyPeriodic || m_zAxiallyPeriodic) {
    std::cerr << m_className << "::UpdatePeriodicity:\n";
    std::cerr << "    Axial periodicity is not possible.\n";
  }

  if (m_xRotationSymmetry || m_yRotationSymmetry || m_zRotationSymmetry) {
    std::cerr << m_className << "::UpdatePeriodicity:\n";
    std::cerr << "    Rotation symmetry is not possible.\n";
  }
}

void ComponentAnalyticField::AddCharge(const double x, const double y,
                                       const double z, const double q) {

  // Convert from fC to internal units (division by 4 pi epsilon0).
  charge3d newCharge;
  newCharge.x = x;
  newCharge.y = y;
  newCharge.z = z;
  newCharge.e = q / FourPiEpsilon0;
  m_ch3d.push_back(newCharge);
}

void ComponentAnalyticField::ClearCharges() {

  m_ch3d.clear();
  m_nTermBessel = 10;
  m_nTermPoly = 100;
}

void ComponentAnalyticField::PrintCharges() const {

  std::cout << m_className << "::PrintCharges:\n";
  if (m_ch3d.empty()) {
    std::cout << "    No charges present.\n";
    return;
  }
  std::cout << "      x [cm]      y [cm]      z [cm]      charge [fC]\n";
  const unsigned int n3d = m_ch3d.size();
  for (unsigned int i = 0; i < n3d; ++i) {
    std::cout << "     " << std::setw(9) << m_ch3d[i].x << "   " << std::setw(9)
              << m_ch3d[i].y << "   " << std::setw(9) << m_ch3d[i].z << "   "
              << std::setw(11) << m_ch3d[i].e * FourPiEpsilon0 << "\n";
  }
}

unsigned int ComponentAnalyticField::GetNumberOfPlanesX() const {

  if (m_ynplan[0] && m_ynplan[1]) {
    return 2;
  } else if (m_ynplan[0] || m_ynplan[1]) {
    return 1;
  }
  return 0;
}

unsigned int ComponentAnalyticField::GetNumberOfPlanesY() const {

  if (m_ynplan[2] && m_ynplan[3]) {
    return 2;
  } else if (m_ynplan[2] || m_ynplan[3]) {
    return 1;
  }
  return 0;
}

bool ComponentAnalyticField::GetWire(const unsigned int i, double& x, double& y,
                                     double& diameter, double& voltage,
                                     std::string& label, double& length,
                                     double& charge, int& ntrap) const {

  if (i >= m_nWires) {
    std::cerr << m_className << "::GetWire:\n";
    std::cerr << "    Wire index is out of range.\n";
    return false;
  }

  x = m_w[i].x;
  y = m_w[i].y;
  diameter = m_w[i].d;
  voltage = m_w[i].v;
  label = m_w[i].type;
  length = m_w[i].u;
  charge = m_w[i].e;
  ntrap = m_w[i].nTrap;
  return true;
}

bool ComponentAnalyticField::GetPlaneX(const unsigned int i, 
                                       double& x, double& voltage,
                                       std::string& label) const {

  if (i >= 2 || (i == 1 && !m_ynplan[1])) {
    std::cerr << m_className << "::GetPlaneX:\n";
    std::cerr << "    Plane index is out of range.\n";
    return false;
  }

  x = m_coplan[i];
  voltage = m_vtplan[i];
  label = planes[i].type;
  return true;
}

bool ComponentAnalyticField::GetPlaneY(const unsigned int i, 
                                       double& y, double& voltage,
                                       std::string& label) const {

  if (i >= 2 || (i == 1 && !m_ynplan[3])) {
    std::cerr << m_className << "::GetPlaneY:\n";
    std::cerr << "    Plane index is out of range.\n";
    return false;
  }

  y = m_coplan[i + 2];
  voltage = m_vtplan[i + 2];
  label = planes[i + 2].type;
  return true;
}

bool ComponentAnalyticField::GetTube(double& r, double& voltage, int& nEdges,
                                     std::string& label) const {

  if (!m_tube) return false;
  r = m_cotube;
  voltage = m_vttube;
  nEdges = m_ntube;
  label = planes[4].type;
  return true;
}

int ComponentAnalyticField::Field(const double xin, const double yin,
                                  const double zin, double& ex, double& ey,
                                  double& ez, double& volt, const bool opt) {

  //-----------------------------------------------------------------------
  //   EFIELD - Subroutine calculating the electric field and the potential
  //            at a given place. It makes use of the routines POT...,
  //            depending on the type of the cell.
  //   VARIABLES : XPOS       : x-coordinate of the place where the field
  //                            is to be calculated.
  //               YPOS, ZPOS : y- and z-coordinates
  //               EX, EY, EZ : x-, y-, z-component of the electric field.
  //               VOLT       : potential at (XPOS,YPOS).
  //               IOPT       : 1 if both E and V are required, 0 if only E
  //                            is to be computed.
  //               ILOC       : Tells where the point is located (0: normal
  //                            I > 0: in wire I, -1: outside a plane,
  //                            -5: in a material, -6: outside the mesh,
  //                            -10: unknown potential).
  //   (Last changed on 28/ 9/07.)
  //-----------------------------------------------------------------------

  // Initialise the field for returns without actual calculations.
  ex = ey = ez = volt = 0.;

  double xpos = xin, ypos = yin;

  // In case of periodicity, move the point into the basic cell.
  if (m_perx) {
    xpos -= m_sx * int(round(xin / m_sx));
  }
  double arot = 0.;
  if (m_pery && m_tube) {
    Cartesian2Polar(xin, yin, xpos, ypos);
    arot = 180. * m_sy * int(round((Pi * ypos) / (m_sy * 180.))) / Pi;
    ypos -= arot;
    Polar2Cartesian(xpos, ypos, xpos, ypos);
  } else if (m_pery) {
    ypos -= m_sy * int(round(yin / m_sy));
  }

  // Move the point to the correct side of the plane.
  if (m_perx && m_ynplan[0] && xpos <= m_coplan[0]) xpos += m_sx;
  if (m_perx && m_ynplan[1] && xpos >= m_coplan[1]) xpos -= m_sx;
  if (m_pery && m_ynplan[2] && ypos <= m_coplan[2]) ypos += m_sy;
  if (m_pery && m_ynplan[3] && ypos >= m_coplan[3]) ypos -= m_sy;

  // In case (XPOS,YPOS) is located behind a plane there is no field.
  if (m_tube) {
    if (!InTube(xpos, ypos, m_cotube, m_ntube)) {
      volt = m_vttube;
      return -4;
    }
  } else {
    if (m_ynplan[0] && xpos < m_coplan[0]) {
      volt = m_vtplan[0];
      return -4;
    }
    if (m_ynplan[1] && xpos > m_coplan[1]) {
      volt = m_vtplan[1];
      return -4;
    }
    if (m_ynplan[2] && ypos < m_coplan[2]) {
      volt = m_vtplan[2];
      return -4;
    }
    if (m_ynplan[3] && ypos > m_coplan[3]) {
      volt = m_vtplan[3];
      return -4;
    }
  }

  // If (xpos, ypos) is within a wire, there is no field either.
  for (int i = m_nWires; i--;) {
    double dx = xpos - m_w[i].x;
    double dy = ypos - m_w[i].y;
    // Correct for periodicities.
    if (m_perx) dx -= m_sx * int(round(dx / m_sx));
    if (m_pery) dy -= m_sy * int(round(dy / m_sy));
    // Check the actual position.
    if (dx * dx + dy * dy < 0.25 * m_w[i].d * m_w[i].d) {
      volt = m_w[i].v;
      return i + 1;
    }
  }

  // Call the appropriate potential calculation function.
  switch (m_cellType) {
    case A00:
      FieldA00(xpos, ypos, ex, ey, volt, opt);
      break;
    case B1X:
      FieldB1X(xpos, ypos, ex, ey, volt, opt);
      break;
    case B1Y:
      FieldB1Y(xpos, ypos, ex, ey, volt, opt);
      break;
    case B2X:
      FieldB2X(xpos, ypos, ex, ey, volt, opt);
      break;
    case B2Y:
      FieldB2Y(xpos, ypos, ex, ey, volt, opt);
      break;
    case C10:
      FieldC10(xpos, ypos, ex, ey, volt, opt);
      break;
    case C2X:
      FieldC2X(xpos, ypos, ex, ey, volt, opt);
      break;
    case C2Y:
      FieldC2Y(xpos, ypos, ex, ey, volt, opt);
      break;
    case C30:
      FieldC30(xpos, ypos, ex, ey, volt, opt);
      break;
    case D10:
      FieldD10(xpos, ypos, ex, ey, volt, opt);
      break;
    case D20:
      FieldD20(xpos, ypos, ex, ey, volt, opt);
      break;
    case D30:
      FieldD30(xpos, ypos, ex, ey, volt, opt);
      break;
    default:
      // Unknown cell type
      std::cerr << m_className << "::Field:\n";
      std::cerr << "    Unknown cell type (id " << m_cellType << ")\n";
      return -10;
      break;
  }

  // Add dipole terms if requested
  if (dipole) {
    double exd = 0., eyd = 0., voltd = 0.;
    switch (m_cellType) {
      case A00:
        // CALL EMCA00(XPOS,YPOS,EXD,EYD,VOLTD,IOPT)
        break;
      case B1X:
        // CALL EMCB1X(XPOS,YPOS,EXD,EYD,VOLTD,IOPT)
        break;
      case B1Y:
        // CALL EMCB1Y(XPOS,YPOS,EXD,EYD,VOLTD,IOPT)
        break;
      case B2X:
        // CALL EMCB2X(XPOS,YPOS,EXD,EYD,VOLTD,IOPT)
        break;
      case B2Y:
        // CALL EMCB2Y(XPOS,YPOS,EXD,EYD,VOLTD,IOPT)
        break;
      default:
        break;
    }
    ex += exd;
    ey += eyd;
    volt += voltd;
  }

  // Rotate the field in some special cases.
  if (m_pery && m_tube) {
    double xaux, yaux;
    Cartesian2Polar(ex, ey, xaux, yaux);
    yaux += arot;
    Polar2Cartesian(xaux, yaux, ex, ey);
  }

  // Correct for the equipotential planes.
  ex -= m_corvta;
  ey -= m_corvtb;
  volt += m_corvta * xpos + m_corvtb * ypos + m_corvtc;

  // Add three dimensional point charges.
  if (!m_ch3d.empty()) {
    double ex3d = 0., ey3d = 0., ez3d = 0., volt3d = 0.;
    switch (m_cellType) {
      case A00:
      case B1X:
      case B1Y:
        Field3dA00(xin, yin, zin, ex3d, ey3d, ez3d, volt3d);
        break;
      case B2X:
        Field3dB2X(xin, yin, zin, ex3d, ey3d, ez3d, volt3d);
        break;
      case B2Y:
        Field3dB2Y(xin, yin, zin, ex3d, ey3d, ez3d, volt3d);
        break;
      case D10:
        Field3dD10(xin, yin, zin, ex3d, ey3d, ez3d, volt3d);
        break;
      default:
        Field3dA00(xin, yin, zin, ex3d, ey3d, ez3d, volt3d);
        break;
    }
    ex += ex3d;
    ey += ey3d;
    ez += ez3d;
    volt += volt3d;
  }

  return 0;
}

void ComponentAnalyticField::CellInit() {

  m_cellset = false;
  m_sigset = false;

  // Coordinate system
  m_polar = false;

  // Cell type
  m_scellType = "A  ";
  m_cellType = A00;

  // Bounding box and voltage range.
  xmin = xmax = 0.;
  ymin = ymax = 0.;
  zmin = zmax = 0.;
  vmin = vmax = 0.;

  // Periodicities
  m_perx = m_pery = false;
  m_sx = m_sy = 1.;

  // Signals
  nFourier = 1;
  m_scellTypeFourier = "A  ";
  fperx = fpery = false;
  mxmin = mxmax = mymin = mymax = 0;
  mfexp = 0;

  m_readout.clear();

  // Wires.
  m_nWires = 0;
  m_w.clear();

  // Force calculation parameters
  weight.clear();
  dens.clear();
  cnalso.clear();

  // Dipole settings
  dipole = false;
  cosph2.clear();
  sinph2.clear();
  amp2.clear();

  // B2 type cells
  b2sin.clear();
  // C type cells
  mode = 0;
  m_zmult = std::complex<double>(0., 0.);
  m_p1 = m_p2 = m_c1 = 0.;
  // D3 type cells
  wmap.clear();
  m_kappa = 0.;
  m_cc1.clear();
  m_cc2.clear();

  // Reference potential
  m_v0 = 0.;
  m_corvta = m_corvtb = m_corvtc = 0.;

  // Planes
  planes.clear();
  planes.resize(5);
  for (int i = 0; i < 4; ++i) {
    m_ynplan[i] = false;
    m_coplan[i] = 0.;
    m_vtplan[i] = 0.;
  }
  // Plane shorthand
  m_ynplax = m_ynplay = false;
  m_coplax = m_coplay = 1.;

  for (int i = 0; i < 5; ++i) {
    planes[i].type = '?';
    planes[i].ind = -1;
    planes[i].ewxcor = 0.;
    planes[i].ewycor = 0.;
    planes[i].strips1.clear();
    planes[i].strips2.clear();
    planes[i].pixels.clear();
  }

  // Tube properties
  m_tube = false;
  m_ntube = 0;
  m_mtube = 1;
  m_cotube = 1.;
  m_vttube = 0.;

  // Capacitance matrices
  m_a.clear();
  m_sigmat.clear();
  m_qplane.clear();

  // 3D charges
  m_ch3d.clear();
  m_nTermBessel = 10;
  m_nTermPoly = 100;

  // Gravity
  down[0] = down[1] = 0.;
  down[2] = 1.;
}

bool ComponentAnalyticField::Prepare() {

  // Check that the cell makes sense.
  if (!CellCheck()) {
    std::cerr << m_className << "::Prepare:\n";
    std::cerr << "    The cell does not meet the requirements.\n";
    return false;
  }
  if (m_debug) {
    std::cout << m_className << "::Prepare:\n";
    std::cout << "    Cell check ok.\n";
  }

  // Determine the cell type.
  if (!CellType()) {
    std::cerr << m_className << "::Prepare:\n";
    std::cerr << "    Type identification of the cell failed.\n";
    return false;
  }
  if (m_debug) {
    std::cout << m_className << "::Prepare:\n";
    std::cout << "    Cell is of type " << m_scellType << ".\n";
  }

  // Calculate the charges.
  if (!Setup()) {
    std::cerr << m_className << "::Prepare:\n";
    std::cerr << "    Calculation of charges failed.\n";
    return false;
  }
  if (m_debug) {
    std::cout << m_className << "::Prepare:\n";
    std::cout << "    Calculation of charges was successful.\n";
  }

  // Assign default gaps for strips and pixels.
  if (!PrepareStrips()) {
    std::cerr << m_className << "::Prepare:\n";
    std::cerr << "    Strip/pixel preparation failed.\n";
    return false;
  }

  m_cellset = true;
  return true;
}

bool ComponentAnalyticField::CellCheck() {

  //-----------------------------------------------------------------------
  //   CELCHK - Subroutine checking the wire positions, The equipotential
  //            planes and the periodicity. Two planes having different
  //            voltages are not allowed to have a common line, wires are
  //            not allowed to be at the same position etc.
  //            This routine determines also the cell-dimensions.
  //   VARIABLE  : WRONG(I)   : .TRUE. if wire I will be removed
  //               IPLAN.     : Number of wires with coord > than plane .
  //   (Last changed on 16/ 2/05.)
  //-----------------------------------------------------------------------

  // Checks on the planes, first move the x planes to the basic cell.
  if (m_perx) {
    double conew1 = m_coplan[0] - m_sx * int(round(m_coplan[0] / m_sx));
    double conew2 = m_coplan[1] - m_sx * int(round(m_coplan[1] / m_sx));
    // Check that they are not one on top of the other.
    if (m_ynplan[0] && m_ynplan[1] && conew1 == conew2) {
      if (conew1 > 0.)
        conew1 -= m_sx;
      else
        conew2 += m_sx;
    }
    // Print some warnings if the planes have been moved.
    if ((conew1 != m_coplan[0] && m_ynplan[0]) ||
        (conew2 != m_coplan[1] && m_ynplan[1])) {
      std::cout << m_className << "::CellCheck:\n";
      std::cout << "    The planes in x or r are moved to the basic period.\n";
      std::cout << "    This should not affect the results.";
    }
    m_coplan[0] = conew1;
    m_coplan[1] = conew2;

    // Two planes should now be separated by SX, cancel PERX if not.
    if (m_ynplan[0] && m_ynplan[1] && fabs(m_coplan[1] - m_coplan[0]) != m_sx) {
      std::cerr << m_className << "::CellCheck:\n";
      std::cerr << "    The separation of the x or r planes"
                << " does not match the period.\b";
      std::cerr << "    The periodicity is cancelled.\n";
      m_perx = false;
    }
    // If there are two planes left, they should have identical V's.
    if (m_ynplan[0] && m_ynplan[1] && m_vtplan[0] != m_vtplan[1]) {
      std::cerr << m_className << "::CellCheck\n";
      std::cerr << "    The voltages of the two x (or r) planes differ.\n";
      std::cerr << "    The periodicity is cancelled.\n";
      m_perx = false;
    }
  }

  // Idem for the y or r planes: move them to the basic period.
  if (m_pery) {
    double conew3 = m_coplan[2] - m_sy * int(round(m_coplan[2] / m_sy));
    double conew4 = m_coplan[3] - m_sy * int(round(m_coplan[3] / m_sy));
    // Check that they are not one on top of the other.
    if (m_ynplan[2] && m_ynplan[3] && conew3 == conew4) {
      if (conew3 > 0.)
        conew3 -= m_sy;
      else
        conew4 += m_sy;
    }
    // Print some warnings if the planes have been moved.
    if ((conew3 != m_coplan[2] && m_ynplan[2]) ||
        (conew4 != m_coplan[3] && m_ynplan[3])) {
      std::cout << m_className << "::CellCheck:\n";
      std::cout << "    The planes in y are moved to the basic period.\n";
      std::cout << "    This should not affect the results.";
    }
    m_coplan[2] = conew3;
    m_coplan[3] = conew4;

    // Two planes should now be separated by SY, cancel PERY if not.
    if (m_ynplan[2] && m_ynplan[3] && fabs(m_coplan[3] - m_coplan[2]) != m_sy) {
      std::cerr << m_className << "::CellCheck:\n";
      std::cerr << "    The separation of the two y planes"
                << " does not match the period.\b";
      std::cerr << "    The periodicity is cancelled.\n";
      m_pery = false;
    }
    // If there are two planes left, they should have identical V's.
    if (m_ynplan[2] && m_ynplan[3] && m_vtplan[2] != m_vtplan[3]) {
      std::cerr << m_className << "::CellCheck\n";
      std::cerr << "    The voltages of the two y planes differ.\n";
      std::cerr << "    The periodicity is cancelled.\n";
      m_pery = false;
    }
  }

  // Check that there is no voltage conflict of crossing planes.
  for (int i = 0; i < 2; ++i) {
    for (int j = 2; j < 3; ++j) {
      if (m_ynplan[i] && m_ynplan[j] && m_vtplan[i] != m_vtplan[j]) {
        std::cerr << m_className << "::CellCheck\n";
        std::cerr << "    Conflicting potential of 2 crossing planes.\n";
        std::cerr << "    One y (or phi) plane is removed.\n";
        m_ynplan[j] = false;
      }
    }
  }

  // Make sure the the coordinates of the planes are properly ordered.
  for (int i = 0; i < 3; i += 2) {
    if (m_ynplan[i] && m_ynplan[i + 1]) {
      if (m_coplan[i] == m_coplan[i + 1]) {
        std::cerr << m_className << "::CellCheck:\n";
        std::cerr << "    Two planes are on top of each other.\n";
        std::cerr << "    One of them is removed.\n";
        m_ynplan[i + 1] = false;
      }
      if (m_coplan[i] > m_coplan[i + 1]) {
        if (m_debug) {
          std::cout << m_className << "::CellCheck:\n";
          std::cout << "    Planes " << i << " and " << i + 1
                    << " are interchanged.\n";
        }
        // Interchange the two planes.
        const double cohlp = m_coplan[i];
        m_coplan[i] = m_coplan[i + 1];
        m_coplan[i + 1] = cohlp;

        const double vthlp = m_vtplan[i];
        m_vtplan[i] = m_vtplan[i + 1];
        m_vtplan[i + 1] = vthlp;

        plane plahlp = planes[i];
        planes[i] = planes[i + 1];
        planes[i + 1] = plahlp;
      }
    }
  }

  // Checks on the wires, start moving them to the basic x period.
  if (m_perx) {
    for (unsigned int i = 0; i < m_nWires; ++i) {
      const double xnew = m_w[i].x - m_sx * int(round(m_w[i].x / m_sx));
      if (int(round(m_w[i].x / m_sx)) != 0) {
        double xprt = m_w[i].x;
        double yprt = m_w[i].y;
        if (m_polar) RTheta2RhoPhi(xprt, yprt, xprt, yprt);
        std::cout << m_className << "::CellCheck:\n";
        std::cout << "    The " << m_w[i].type << "-wire at (" << xprt << ", "
                  << yprt << ") is moved to the basic x (or r) period.\n";
        std::cout << "    This should not affect the results.\n";
      }
      m_w[i].x = xnew;
    }
  }

  // In case of y-periodicity, all wires should be in the first y-period.
  if (m_tube && m_pery) {
    for (unsigned int i = 0; i < m_nWires; ++i) {
      double xnew = m_w[i].x;
      double ynew = m_w[i].y;
      Cartesian2Polar(xnew, ynew, xnew, ynew);
      if (int(round((Pi / ynew) / (m_sy * 180.))) != 0) {
        std::cout << m_className << "::CellCheck:\n";
        std::cout << "    The " << m_w[i].type << "-wire at (" << m_w[i].x
                  << ", " << m_w[i].y
                  << ") is moved to the basic phi period.\n";
        std::cout << "    This should not affect the results.\n";
        ynew -= 180 * m_sy * int(round((Pi * ynew) / (m_sy * 180.))) / Pi;
        Polar2Cartesian(xnew, ynew, m_w[i].x, m_w[i].y);
      }
    }
  } else if (m_pery) {
    for (unsigned int i = 0; i < m_nWires; ++i) {
      double ynew = m_w[i].y - m_sy * int(round(m_w[i].y / m_sy));
      if (int(round(m_w[i].y / m_sy)) != 0) {
        double xprt = m_w[i].x;
        double yprt = m_w[i].y;
        if (m_polar) RTheta2RhoPhi(m_w[i].x, m_w[i].y, xprt, yprt);
        std::cout << m_className << "::CellCheck:\n";
        std::cout << "    The " << m_w[i].type << "-wire at (" << xprt << ", "
                  << yprt << ") is moved to the basic y period.\n";
        std::cout << "    This should not affect the results.\n";
      }
      m_w[i].y = ynew;
    }
  }

  // Make sure the plane numbering is standard: P1 wires P2, P3 wires P4.
  int iplan1 = 0, iplan2 = 0, iplan3 = 0, iplan4 = 0;
  for (unsigned int i = 0; i < m_nWires; ++i) {
    if (m_ynplan[0] && m_w[i].x <= m_coplan[0]) ++iplan1;
    if (m_ynplan[1] && m_w[i].x <= m_coplan[1]) ++iplan2;
    if (m_ynplan[2] && m_w[i].y <= m_coplan[2]) ++iplan3;
    if (m_ynplan[3] && m_w[i].y <= m_coplan[3]) ++iplan4;
  }

  // Find out whether smaller (-1) or larger (+1) coord. are to be kept.
  if (m_ynplan[0] && m_ynplan[1]) {
    if (iplan1 > int(m_nWires) / 2) {
      m_ynplan[1] = false;
      iplan1 = -1;
    } else {
      iplan1 = +1;
    }
    if (iplan2 < int(m_nWires) / 2) {
      m_ynplan[0] = false;
      iplan2 = +1;
    } else {
      iplan2 = -1;
    }
  }
  if (m_ynplan[0] && !m_ynplan[1]) {
    if (iplan1 > int(m_nWires) / 2)
      iplan1 = -1;
    else
      iplan1 = +1;
  }
  if (m_ynplan[1] && !m_ynplan[0]) {
    if (iplan2 < int(m_nWires) / 2)
      iplan2 = +1;
    else
      iplan2 = -1;
  }

  if (m_ynplan[2] && m_ynplan[3]) {
    if (iplan3 > int(m_nWires) / 2) {
      m_ynplan[3] = false;
      iplan3 = -1;
    } else {
      iplan3 = +1;
    }
    if (iplan4 < int(m_nWires) / 2) {
      m_ynplan[2] = false;
      iplan4 = +1;
    } else {
      iplan4 = -1;
    }
  }
  if (m_ynplan[2] && !m_ynplan[3]) {
    if (iplan3 > int(m_nWires) / 2)
      iplan3 = -1;
    else
      iplan3 = +1;
  }
  if (m_ynplan[3] && !m_ynplan[2]) {
    if (iplan4 < int(m_nWires) / 2)
      iplan4 = +1;
    else
      iplan4 = -1;
  }

  // Adapt the numbering of the planes if necessary.
  if (iplan1 == -1) {
    m_ynplan[0] = false;
    m_ynplan[1] = true;
    m_coplan[1] = m_coplan[0];
    m_vtplan[1] = m_vtplan[0];
    planes[1] = planes[0];
  }

  if (iplan2 == +1) {
    m_ynplan[1] = false;
    m_ynplan[0] = true;
    m_coplan[0] = m_coplan[1];
    m_vtplan[0] = m_vtplan[1];
    planes[0] = planes[1];
  }

  if (iplan3 == -1) {
    m_ynplan[2] = false;
    m_ynplan[3] = true;
    m_coplan[3] = m_coplan[2];
    m_vtplan[3] = m_vtplan[2];
    planes[3] = planes[2];
  }

  if (iplan4 == +1) {
    m_ynplan[3] = false;
    m_ynplan[2] = true;
    m_coplan[2] = m_coplan[3];
    m_vtplan[2] = m_vtplan[3];
    planes[2] = planes[3];
  }

  std::vector<bool> wrong(m_nWires, false);
  // Second pass for the wires, check position relative to the planes.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    if (m_ynplan[0] && m_w[i].x - 0.5 * m_w[i].d <= m_coplan[0]) wrong[i] = true;
    if (m_ynplan[1] && m_w[i].x + 0.5 * m_w[i].d >= m_coplan[1]) wrong[i] = true;
    if (m_ynplan[2] && m_w[i].y - 0.5 * m_w[i].d <= m_coplan[2]) wrong[i] = true;
    if (m_ynplan[3] && m_w[i].y + 0.5 * m_w[i].d >= m_coplan[3]) wrong[i] = true;
    if (m_tube) {
      if (!InTube(m_w[i].x, m_w[i].y, m_cotube, m_ntube)) {
        std::cerr << m_className << "::CellCheck:\n";
        std::cerr << "    The " << m_w[i].type << "-wire at (" << m_w[i].x
                  << ", " << m_w[i].y << ") is located outside the tube.\n";
        std::cerr << "    This wire is removed.\n";
        wrong[i] = true;
      }
    } else if (wrong[i]) {
      double xprt = m_w[i].x;
      double yprt = m_w[i].y;
      if (m_polar) RTheta2RhoPhi(xprt, yprt, xprt, yprt);
      std::cerr << m_className << "::CellCheck:\n";
      std::cerr << "    The " << m_w[i].type << "-wire at (" << xprt << ", "
                << yprt << ") is located outside the planes.\n";
      std::cerr << "    This wire is removed.\n";
    } else if ((m_perx && m_w[i].d >= m_sx) || (m_pery && m_w[i].d >= m_sy)) {
      double xprt = m_w[i].x;
      double yprt = m_w[i].y;
      if (m_polar) RTheta2RhoPhi(xprt, yprt, xprt, yprt);
      std::cerr << m_className << "::CellCheck:\n";
      std::cerr << "    The diameter of the " << m_w[i].type << "-wire at ("
                << xprt << ", " << yprt << ") exceeds 1 period.\n";
      std::cerr << "    This wire is removed.\n";
      wrong[i] = true;
    }
  }

  // Check the wire spacing.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    if (wrong[i]) continue;
    for (unsigned int j = i + 1; j < m_nWires; ++j) {
      if (wrong[j]) continue;
      double xsepar = 0.;
      double ysepar = 0.;
      if (m_tube) {
        if (m_pery) {
          double xaux1, xaux2, yaux1, yaux2;
          Cartesian2Polar(m_w[i].x, m_w[i].y, xaux1, yaux1);
          Cartesian2Polar(m_w[j].x, m_w[j].y, xaux2, yaux2);
          yaux1 -= m_sy * int(round(yaux1 / m_sy));
          yaux2 -= m_sy * int(round(yaux2 / m_sy));
          Polar2Cartesian(xaux1, yaux1, xaux1, yaux1);
          Polar2Cartesian(xaux2, yaux2, xaux2, yaux2);
          xsepar = xaux1 - xaux2;
          ysepar = yaux1 - yaux2;
        } else {
          xsepar = m_w[i].x - m_w[j].x;
          ysepar = m_w[i].y - m_w[j].y;
        }
      } else {
        xsepar = fabs(m_w[i].x - m_w[j].x);
        if (m_perx) xsepar -= m_sx * int(round(xsepar / m_sx));
        ysepar = fabs(m_w[i].y - m_w[j].y);
        if (m_pery) ysepar -= m_sy * int(round(ysepar / m_sy));
      }
      if (xsepar * xsepar + ysepar * ysepar <
          0.25 * pow(m_w[i].d + m_w[j].d, 2)) {
        double xprti = m_w[i].x;
        double yprti = m_w[i].y;
        double xprtj = m_w[j].x;
        double yprtj = m_w[j].y;
        if (m_polar) RTheta2RhoPhi(xprti, yprti, xprti, yprti);
        if (m_polar) RTheta2RhoPhi(xprtj, yprtj, xprtj, yprtj);
        std::cerr << m_className << "::CellCheck:\n";
        std::cerr << "    The " << m_w[i].type << "-wire at (" << xprti << ", "
                  << yprti << ")\n"
                  << "    and the " << m_w[j].type << "-wire at (" << xprtj
                  << ", " << yprtj << ") overlap at least partially.\n";
        std::cerr << "    The latter wire is removed.\n";
        wrong[j] = true;
      }
    }
  }

  // Remove the wires which are not acceptable for one reason or another.
  const int iWires = m_nWires;
  m_nWires = 0;
  for (int i = 0; i < iWires; ++i) {
    if (!wrong[i]) {
      m_w[m_nWires].x = m_w[i].x;
      m_w[m_nWires].y = m_w[i].y;
      m_w[m_nWires].d = m_w[i].d;
      m_w[m_nWires].v = m_w[i].v;
      m_w[m_nWires].type = m_w[i].type;
      m_w[m_nWires].u = m_w[i].u;
      m_w[m_nWires].e = m_w[i].e;
      m_w[m_nWires].ind = m_w[i].ind;
      m_w[m_nWires].nTrap = m_w[i].nTrap;
      ++m_nWires;
    }
  }

  // Ensure that some elements are left.
  int nElements = m_nWires;
  if (m_ynplan[0]) ++nElements;
  if (m_ynplan[1]) ++nElements;
  if (m_ynplan[2]) ++nElements;
  if (m_ynplan[3]) ++nElements;
  if (m_tube) ++nElements;

  if (nElements < 2) {
    std::cerr << m_className << "::CellCheck:\n";
    std::cerr << "    At least 2 elements are necessary.\n";
    std::cerr << "    Cell rejected.\n";
    return false;
  }

  // Determine maximum and minimum coordinates and potentials.
  bool setx = false;
  bool sety = false;
  bool setz = false;
  bool setv = false;

  xmin = xmax = 0.;
  ymin = ymax = 0.;
  zmin = zmax = 0.;
  vmin = vmax = 0.;

  // Loop over the wires.
  for (int i = m_nWires; i--;) {
    if (setx) {
      xmin = std::min(xmin, m_w[i].x - m_w[i].d / 2.);
      xmax = std::max(xmax, m_w[i].x + m_w[i].d / 2.);
    } else {
      xmin = m_w[i].x - m_w[i].d / 2.;
      xmax = m_w[i].x + m_w[i].d / 2.;
      setx = true;
    }
    if (sety) {
      ymin = std::min(ymin, m_w[i].y - m_w[i].d / 2.);
      ymax = std::max(ymax, m_w[i].y + m_w[i].d / 2.);
    } else {
      ymin = m_w[i].y - m_w[i].d / 2.;
      ymax = m_w[i].y + m_w[i].d / 2.;
      sety = true;
    }
    if (setz) {
      zmin = std::min(zmin, -m_w[i].u / 2.);
      zmax = std::max(zmax, +m_w[i].u / 2.);
    } else {
      zmin = -m_w[i].u / 2.;
      zmax = +m_w[i].u / 2.;
      setz = true;
    }
    if (setv) {
      vmin = std::min(vmin, m_w[i].v);
      vmax = std::max(vmax, m_w[i].v);
    } else {
      vmin = vmax = m_w[i].v;
      setv = true;
    }
  }
  // Consider the planes.
  for (int i = 0; i < 4; ++i) {
    if (!m_ynplan[i]) continue;
    if (i < 2) {
      if (setx) {
        xmin = std::min(xmin, m_coplan[i]);
        xmax = std::max(xmax, m_coplan[i]);
      } else {
        xmin = xmax = m_coplan[i];
        setx = true;
      }
    } else {
      if (sety) {
        ymin = std::min(ymin, m_coplan[i]);
        ymax = std::max(ymax, m_coplan[i]);
      } else {
        ymin = ymax = m_coplan[i];
        sety = true;
      }
    }
    if (setv) {
      vmin = std::min(vmin, m_vtplan[i]);
      vmax = std::max(vmax, m_vtplan[i]);
    } else {
      vmin = vmax = m_vtplan[i];
      setv = true;
    }
  }

  // Consider the tube.
  if (m_tube) {
    xmin = -1.1 * m_cotube;
    xmax = +1.1 * m_cotube;
    setx = true;
    ymin = -1.1 * m_cotube;
    ymax = +1.1 * m_cotube;
    sety = true;
    vmin = std::min(vmin, m_vttube);
    vmax = std::max(vmax, m_vttube);
    setv = true;
  }

  // In case of x-periodicity, XMAX-XMIN should be SX,
  if (m_perx && m_sx > (xmax - xmin)) {
    xmin = -m_sx / 2.;
    xmax = m_sx / 2.;
    setx = true;
  }
  // in case of y-periodicity, YMAX-YMIN should be SY,
  if (m_pery && m_sy > (ymax - ymin)) {
    ymin = -m_sy / 2.;
    ymax = m_sy / 2.;
    sety = true;
  }
  // in case the cell is polar, the y range should be < 2 pi.
  if (m_polar && (ymax - ymin) >= TwoPi) {
    ymin = -Pi;
    ymax = +Pi;
    sety = true;
  }

  // Fill in missing dimensions.
  if (setx && xmin != xmax && (ymin == ymax || !sety)) {
    ymin -= fabs(xmax - xmin) / 2.;
    ymax += fabs(xmax - xmin) / 2.;
    sety = true;
  }
  if (sety && ymin != ymax && (xmin == xmax || !setx)) {
    xmin -= fabs(ymax - ymin) / 2.;
    xmax += fabs(ymax - ymin) / 2.;
    setx = true;
  }

  if (!setz) {
    zmin = -(fabs(xmax - xmin) + fabs(ymax - ymin)) / 4.;
    zmax = +(fabs(xmax - xmin) + fabs(ymax - ymin)) / 4.;
    setz = true;
  }

  // Ensure that all dimensions are now set.
  if (!(setx && sety && setz)) {
    std::cerr << m_className << "::CellCheck:\n";
    std::cerr << "    Unable to establish"
              << " default dimensions in all directions.\n";
  }

  // Check that at least some different voltages are present.
  if (vmin == vmax || !setv) {
    std::cerr << m_className << "::CellCheck:\n";
    std::cerr << "    All potentials in the cell are the same.\n";
    std::cerr << "    There is no point in going on.\n";
    return false;
  }

  // Cell seems to be alright since it passed all critical tests.
  return true;
}

bool ComponentAnalyticField::CellType() {

  // Tube geometries
  if (m_tube) {
    if (m_ntube == 0) {
      if (m_pery) {
        m_scellType = "D2 ";
        m_cellType = D20;
      } else {
        m_scellType = "D1 ";
        m_cellType = D10;
      }
    } else if (m_ntube >= 3 && m_ntube <= 8) {
      if (m_pery) {
        m_scellType = "D4 ";
        m_cellType = D40;
      } else {
        m_scellType = "D3 ";
        m_cellType = D30;
      }
    } else {
      std::cerr << m_className << "::CellType:\n"
                << "    Potentials for tube with " << m_ntube
                << " edges are not yet available.\n"
                << "    Using a round tube instead.\n";
      m_scellType = "D3 ";
      m_ntube = 0;
      m_cellType = D30;
    }
    return true;
  }

  // Find the 'A' type cell.
  if (!(m_perx || m_pery) && !(m_ynplan[0] && m_ynplan[1]) &&
      !(m_ynplan[2] && m_ynplan[3])) {
    m_scellType = "A  ";
    m_cellType = A00;
    return true;
  }

  // Find the 'B1X' type cell.
  if (m_perx && !m_pery && !(m_ynplan[0] || m_ynplan[1]) &&
      !(m_ynplan[2] && m_ynplan[3])) {
    m_scellType = "B1X";
    m_cellType = B1X;
    return true;
  }

  // Find the 'B1Y' type cell.
  if (m_pery && !m_perx && !(m_ynplan[0] && m_ynplan[1]) &&
      !(m_ynplan[2] || m_ynplan[3])) {
    m_scellType = "B1Y";
    m_cellType = B1Y;
    return true;
  }

  // Find the 'B2X' type cell.
  if (m_perx && !m_pery && !(m_ynplan[2] && m_ynplan[3])) {
    m_scellType = "B2X";
    m_cellType = B2X;
    return true;
  }

  if (!(m_perx || m_pery) && !(m_ynplan[2] && m_ynplan[3]) &&
      (m_ynplan[0] && m_ynplan[1])) {
    m_sx = fabs(m_coplan[1] - m_coplan[0]);
    m_scellType = "B2X";
    m_cellType = B2X;
    return true;
  }

  // Find the 'B2Y' type cell.
  if (m_pery && !m_perx && !(m_ynplan[0] && m_ynplan[1])) {
    m_scellType = "B2Y";
    m_cellType = B2Y;
    return true;
  }

  if (!(m_perx || m_pery) && !(m_ynplan[0] && m_ynplan[1]) &&
      (m_ynplan[2] && m_ynplan[3])) {
    m_sy = fabs(m_coplan[3] - m_coplan[2]);
    m_scellType = "B2Y";
    m_cellType = B2Y;
    return true;
  }

  // Find the 'C1 ' type cell.
  if (!(m_ynplan[0] || m_ynplan[1] || m_ynplan[2] || m_ynplan[3]) && m_perx && m_pery) {
    m_scellType = "C1 ";
    m_cellType = C10;
    return true;
  }

  // Find the 'C2X' type cell.
  if (!((m_ynplan[2] && m_pery) || (m_ynplan[2] && m_ynplan[3]))) {
    if (m_ynplan[0] && m_ynplan[1]) {
      m_sx = fabs(m_coplan[1] - m_coplan[0]);
      m_scellType = "C2X";
      m_cellType = C2X;
      return true;
    }
    if (m_perx && m_ynplan[0]) {
      m_scellType = "C2X";
      m_cellType = C2X;
      return true;
    }
  }

  // Find the 'C2Y' type cell.
  if (!((m_ynplan[0] && m_perx) || (m_ynplan[0] && m_ynplan[1]))) {
    if (m_ynplan[2] && m_ynplan[3]) {
      m_sy = fabs(m_coplan[3] - m_coplan[2]);
      m_scellType = "C2Y";
      m_cellType = C2Y;
      return true;
    }
    if (m_pery && m_ynplan[2]) {
      m_scellType = "C2Y";
      m_cellType = C2Y;
      return true;
    }
  }

  // Find the 'C3 ' type cell.
  if (m_perx && m_pery) {
    m_scellType = "C3 ";
    m_cellType = C30;
    return true;
  }

  if (m_perx) {
    m_sy = fabs(m_coplan[3] - m_coplan[2]);
    m_scellType = "C3 ";
    m_cellType = C30;
    return true;
  }

  if (m_pery) {
    m_sx = fabs(m_coplan[1] - m_coplan[0]);
    m_scellType = "C3 ";
    m_cellType = C30;
    return true;
  }

  if (m_ynplan[0] && m_ynplan[1] && m_ynplan[2] && m_ynplan[3]) {
    m_scellType = "C3 ";
    m_sx = fabs(m_coplan[1] - m_coplan[0]);
    m_sy = fabs(m_coplan[3] - m_coplan[2]);
    m_cellType = C30;
    return true;
  }

  // Cell is not recognised.
  return false;
}

bool ComponentAnalyticField::PrepareStrips() {

  // -----------------------------------------------------------------------
  //    CELSTR - Assigns default anode-cathode gaps, if applicable.
  //    (Last changed on  7/12/00.)
  // -----------------------------------------------------------------------

  double gapDef[4] = {0., 0., 0., 0.};

  // Compute default gaps.
  if (m_ynplan[0]) {
    if (m_ynplan[1]) {
      gapDef[0] = m_coplan[1] - m_coplan[0];
    } else if (m_nWires <= 0) {
      gapDef[0] = -1.;
    } else {
      gapDef[0] = m_w[0].x - m_coplan[0];
      for (int i = m_nWires; i--;) {
        if (m_w[i].x - m_coplan[0] < gapDef[0]) gapDef[0] = m_w[i].x - m_coplan[0];
      }
    }
  }

  if (m_ynplan[1]) {
    if (m_ynplan[0]) {
      gapDef[1] = m_coplan[1] - m_coplan[0];
    } else if (m_nWires <= 0) {
      gapDef[1] = -1.;
    } else {
      gapDef[1] = m_coplan[1] - m_w[0].x;
      for (int i = m_nWires; i--;) {
        if (m_coplan[1] - m_w[i].x < gapDef[1]) gapDef[1] = m_coplan[1] - m_w[i].x;
      }
    }
  }

  if (m_ynplan[2]) {
    if (m_ynplan[3]) {
      gapDef[2] = m_coplan[3] - m_coplan[2];
    } else if (m_nWires <= 0) {
      gapDef[2] = -1.;
    } else {
      gapDef[2] = m_w[0].y - m_coplan[2];
      for (int i = m_nWires; i--;) {
        if (m_w[i].y - m_coplan[2] < gapDef[2]) gapDef[2] = m_w[i].y - m_coplan[2];
      }
    }
  }

  if (m_ynplan[3]) {
    if (m_ynplan[2]) {
      gapDef[3] = m_coplan[3] - m_coplan[2];
    } else if (m_nWires <= 0) {
      gapDef[3] = -1.;
    } else {
      gapDef[3] = m_coplan[3] - m_w[0].y;
      for (int i = m_nWires; i--;) {
        if (m_coplan[3] - m_w[i].y < gapDef[3]) gapDef[3] = m_coplan[3] - m_w[i].y;
      }
    }
  }

  // Assign.
  for (unsigned int i = 0; i < 4; ++i) {
    const unsigned int nStrips1 = planes[i].strips1.size();
    for (unsigned int j = 0; j < nStrips1; ++j) {
      if (planes[i].strips1[j].gap < 0.) {
        planes[i].strips1[j].gap = gapDef[i];
      }
      if (planes[i].strips1[j].gap < 0.) {
        std::cerr << m_className << "::PrepareStrips:\n";
        std::cerr << "    Not able to set a default anode-cathode gap\n";
        std::cerr << "    for x/y-strip " << j << " of plane " << i << ".\n";
        return false;
      }
    }
    const unsigned int nStrips2 = planes[i].strips2.size();
    for (unsigned int j = 0; j < nStrips2; ++j) {
      if (planes[i].strips2[j].gap < 0.) {
        planes[i].strips2[j].gap = gapDef[i];
      }
      if (planes[i].strips2[j].gap < 0.) {
        std::cerr << m_className << "::PrepareStrips:\n";
        std::cerr << "    Not able to set a default anode-cathode gap\n";
        std::cerr << "    for z-strip " << j << " of plane " << i << ".\n";
        return false;
      }
    }
    const unsigned int nPixels = planes[i].pixels.size();
    for (unsigned int j = 0; j < nPixels; ++j) {
      if (planes[i].pixels[j].gap < 0.) {
        planes[i].pixels[j].gap = gapDef[i];
      }
      if (planes[i].pixels[j].gap < 0.) {
        std::cerr << m_className << "::PrepareStrips:\n";
        std::cerr << "    Not able to set a default anode-cathode gap\n";
        std::cerr << "    for pixel " << j << " of plane " << i << ".\n";
        return false;
      }
    }
  }

  return true;
}

void ComponentAnalyticField::AddReadout(const std::string& label) {

  // Check if this readout group already exists.
  if (std::find(m_readout.begin(), m_readout.end(), label) != m_readout.end()) {
    std::cout << m_className << "::AddReadout:\n";
    std::cout << "    Readout group " << label << " already exists.\n";
    return;
  }
  m_readout.push_back(label);

  unsigned int nWiresFound = 0;
  for (unsigned int i = 0; i < m_nWires; ++i) {
    if (m_w[i].type == label) ++nWiresFound;
  }

  unsigned int nPlanesFound = 0;
  unsigned int nStripsFound = 0;
  unsigned int nPixelsFound = 0;
  for (int i = 0; i < 5; ++i) {
    if (planes[i].type == label) ++nPlanesFound;
    const unsigned int nStrips1 = planes[i].strips1.size();
    for (unsigned int j = 0; j < nStrips1; ++j) {
      if (planes[i].strips1[j].type == label) ++nStripsFound;
    }
    const unsigned int nStrips2 = planes[i].strips2.size();
    for (unsigned int j = 0; j < nStrips2; ++j) {
      if (planes[i].strips2[j].type == label) ++nStripsFound;
    }
    const unsigned int nPixels = planes[i].pixels.size();
    for (unsigned int j = 0; j < nPixels; ++j) {
      if (planes[i].pixels[j].type == label) ++nPixelsFound;
    }
  }

  if (nWiresFound == 0 && nPlanesFound == 0 && nStripsFound == 0 &&
      nPixelsFound == 0) {
    std::cerr << m_className << "::AddReadout:\n";
    std::cerr << "    At present there are no wires, planes or strips\n";
    std::cerr << "    associated to readout group " << label << ".\n";
  } else {
    std::cout << m_className << "::AddReadout:\n";
    std::cout << "    Readout group " << label << " comprises:\n";
    if (nWiresFound > 1) {
      std::cout << "      " << nWiresFound << " wires\n";
    } else if (nWiresFound == 1) {
      std::cout << "      1 wire\n";
    }
    if (nPlanesFound > 1) {
      std::cout << "      " << nPlanesFound << " planes\n";
    } else if (nPlanesFound == 1) {
      std::cout << "      1 plane\n";
    }
    if (nStripsFound > 1) {
      std::cout << "      " << nStripsFound << " strips\n";
    } else if (nStripsFound == 1) {
      std::cout << "      1 strip\n";
    }
    if (nPixelsFound > 1) {
      std::cout << "      " << nPixelsFound << " pixels\n";
    } else if (nPixelsFound == 1) {
      std::cout << "      1 pixel\n";
    }
  }

  m_sigset = false;
}

bool ComponentAnalyticField::Setup() {

  //-----------------------------------------------------------------------
  //     SETUP  - Routine calling the appropriate setup routine.
  //     (Last changed on 19/ 9/07.)
  //-----------------------------------------------------------------------

  // Set a separate set of plane variables to avoid repeated loops.
  if (m_ynplan[0]) {
    m_coplax = m_coplan[0];
    m_ynplax = true;
  } else if (m_ynplan[1]) {
    m_coplax = m_coplan[1];
    m_ynplax = true;
  } else {
    m_ynplax = false;
  }

  if (m_ynplan[2]) {
    m_coplay = m_coplan[2];
    m_ynplay = true;
  } else if (m_ynplan[3]) {
    m_coplay = m_coplan[3];
    m_ynplay = true;
  } else {
    m_ynplay = false;
  }

  // Set the correction parameters for the planes.
  if (m_tube) {
    m_corvta = 0.;
    m_corvtb = 0.;
    m_corvtc = m_vttube;
  } else if ((m_ynplan[0] && m_ynplan[1]) && !(m_ynplan[2] || m_ynplan[3])) {
    m_corvta = (m_vtplan[0] - m_vtplan[1]) / (m_coplan[0] - m_coplan[1]);
    m_corvtb = 0.;
    m_corvtc = (m_vtplan[1] * m_coplan[0] - m_vtplan[0] * m_coplan[1]) /
             (m_coplan[0] - m_coplan[1]);
  } else if ((m_ynplan[2] && m_ynplan[3]) && !(m_ynplan[0] || m_ynplan[1])) {
    m_corvta = 0.;
    m_corvtb = (m_vtplan[2] - m_vtplan[3]) / (m_coplan[2] - m_coplan[3]);
    m_corvtc = (m_vtplan[3] * m_coplan[2] - m_vtplan[2] * m_coplan[3]) /
             (m_coplan[2] - m_coplan[3]);
  } else {
    m_corvta = m_corvtb = m_corvtc = 0.;
    if (m_ynplan[0]) m_corvtc = m_vtplan[0];
    if (m_ynplan[1]) m_corvtc = m_vtplan[1];
    if (m_ynplan[2]) m_corvtc = m_vtplan[2];
    if (m_ynplan[3]) m_corvtc = m_vtplan[3];
  }

  // Skip wire calculations if there aren't any.
  if (m_nWires <= 0) return true;

  // Redimension the capacitance matrix
  m_a.assign(m_nWires, std::vector<double>(m_nWires, 0.));

  bool ok = true;

  // Call the set routine appropriate for the present cell type.
  if (m_scellType == "A  ") ok = SetupA00();
  if (m_scellType == "B1X") ok = SetupB1X();
  if (m_scellType == "B1Y") ok = SetupB1Y();
  if (m_scellType == "B2X") ok = SetupB2X();
  if (m_scellType == "B2Y") ok = SetupB2Y();
  if (m_scellType == "C1 ") ok = SetupC10();
  if (m_scellType == "C2X") ok = SetupC2X();
  if (m_scellType == "C2Y") ok = SetupC2Y();
  if (m_scellType == "C3 ") ok = SetupC30();
  if (m_scellType == "D1 ") ok = SetupD10();
  if (m_scellType == "D2 ") ok = SetupD20();
  if (m_scellType == "D3 ") ok = SetupD30();

  // Add dipole terms if required
  if (ok && dipole) {
    ok = SetupDipole();
    if (!ok) {
      std::cerr << m_className << "::Setup:\n";
      std::cerr << "    Computing the dipole moments failed.\n";
    }
  }

  m_a.clear();

  if (!ok) {
    std::cerr << m_className << "::Setup:\n";
    std::cerr << "    Preparing the cell for field calculations"
              << " did not succeed.\n";
    return false;
  }
  return true;
}

bool ComponentAnalyticField::SetupA00() {

  //-----------------------------------------------------------------------
  //   SETA00 - Subroutine preparing the field calculations by calculating
  //            the charges on the wires, for the cell with one charge and
  //            not more than one plane in either x or y.
  //            The potential used is log(r).
  //-----------------------------------------------------------------------

  // Loop over all wire combinations.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    m_a[i][i] = 0.25 * m_w[i].d * m_w[i].d;
    // Take care of the equipotential planes.
    if (m_ynplax) m_a[i][i] /= 4. * pow(m_w[i].x - m_coplax, 2);
    if (m_ynplay) m_a[i][i] /= 4. * pow(m_w[i].y - m_coplay, 2);
    // Take care of combinations of equipotential planes.
    if (m_ynplax && m_ynplay)
      m_a[i][i] *=
          4.0 * (pow(m_w[i].x - m_coplax, 2) + pow(m_w[i].y - m_coplay, 2));
    // Define the final version of a[i][i].
    m_a[i][i] = -0.5 * log(m_a[i][i]);
    // Loop over all other wires for the off-diagonal elements.
    for (unsigned int j = i + 1; j < m_nWires; ++j) {
      m_a[i][j] = pow(m_w[i].x - m_w[j].x, 2) + pow(m_w[i].y - m_w[j].y, 2);
      // Take care of equipotential planes.
      if (m_ynplax)
        m_a[i][j] = m_a[i][j] / (pow(m_w[i].x + m_w[j].x - 2. * m_coplax, 2) +
                                 pow(m_w[i].y - m_w[j].y, 2));
      if (m_ynplay)
        m_a[i][j] = m_a[i][j] / (pow(m_w[i].x - m_w[j].x, 2) +
                                 pow(m_w[i].y + m_w[j].y - 2. * m_coplay, 2));
      // Take care of pairs of equipotential planes in different directions.
      if (m_ynplax && m_ynplay)
        m_a[i][j] *= pow(m_w[i].x + m_w[j].x - 2. * m_coplax, 2) +
                     pow(m_w[i].y + m_w[j].y - 2. * m_coplay, 2);
      // Define a final version of a[i][j].
      m_a[i][j] = -0.5 * log(m_a[i][j]);
      // Copy this to a[j][i] since the capacitance matrix is symmetric.
      m_a[j][i] = m_a[i][j];
    }
  }
  // Call CHARGE to calculate the charges really.
  return Charge();
}

bool ComponentAnalyticField::SetupB1X() {

  //-----------------------------------------------------------------------
  //   SETB1X - Routine preparing the field calculations by filling the
  //            c-matrix, the potential used is re(log(sin Pi/s (z-z0))).
  //   VARIABLES : xx         : Difference in x of two wires * factor.
  //               yy         : Difference in y of two wires * factor.
  //               yymirr     : Difference in y of one wire and the mirror
  //                            image of another * factor.
  //               r2plan     : Periodic length of (xx,yymirr)
  //-----------------------------------------------------------------------

  double xx = 0., yy = 0., yymirr = 0.;
  double r2plan = 0.;

  // Loop over all wires and calculate the diagonal elements first.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    m_a[i][i] = -log(0.5 * m_w[i].d * Pi / m_sx);
    // Take care of a plane at constant y if it exist.
    if (m_ynplay) {
      yy = (Pi / m_sx) * 2. * (m_w[i].y - m_coplay);
      if (fabs(yy) > 20.) m_a[i][i] += fabs(yy) - CLog2;
      if (fabs(yy) <= 20.) m_a[i][i] += log(fabs(sinh(yy)));
    }
    // Loop over all other wires to obtain off-diagonal elements.
    for (unsigned int j = i + 1; j < m_nWires; ++j) {
      xx = (Pi / m_sx) * (m_w[i].x - m_w[j].x);
      yy = (Pi / m_sx) * (m_w[i].y - m_w[j].y);
      if (fabs(yy) > 20.) m_a[i][j] = -fabs(yy) + CLog2;
      if (fabs(yy) <= 20.) {
        const double sinhy = sinh(yy);
        const double sinx = sin(xx);
        m_a[i][j] = -0.5 * log(sinhy * sinhy + sinx * sinx);
      }
      // Take equipotential planes into account if they exist.
      if (m_ynplay) {
        r2plan = 0.;
        yymirr = (Pi / m_sx) * (m_w[i].y + m_w[j].y - 2. * m_coplay);
        if (fabs(yymirr) > 20.) r2plan = fabs(yymirr) - CLog2;
        if (fabs(yymirr) <= 20.) {
          const double sinhy = sinh(yymirr);
          const double sinx = sin(xx);
          r2plan = 0.5 * log(sinhy * sinhy + sinx * sinx);
        }
        m_a[i][j] += r2plan;
      }
      // Copy a[i][j] to a[j][i], the capactance matrix is symmetric.
      m_a[j][i] = m_a[i][j];
    }
  }
  // Call function CHARGE calculating all kinds of useful things.
  return Charge();
}

bool ComponentAnalyticField::SetupB1Y() {

  //-----------------------------------------------------------------------
  //   SETB1Y - Routine preparing the field calculations by setting the
  //            charges. The potential used is Re log(sinh Pi/sy(z-z0)).
  //   VARIABLES : yy         : Difference in y of two wires * factor.
  //               xxmirr     : Difference in x of one wire and the mirror
  //                            image of another * factor.
  //               r2plan     : Periodic length of (xxmirr,yy).
  //-----------------------------------------------------------------------

  double xx = 0., yy = 0., xxmirr = 0.;
  double r2plan = 0.;

  // Loop over all wires and calculate the diagonal elements first.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    m_a[i][i] = -log(0.5 * m_w[i].d * Pi / m_sy);
    // Take care of planes 1 and 2 if present.
    if (m_ynplax) {
      xx = (Pi / m_sy) * 2. * (m_w[i].x - m_coplax);
      if (fabs(xx) > 20.) m_a[i][i] += fabs(xx) - CLog2;
      if (fabs(xx) <= 20.) m_a[i][i] += log(fabs(sinh(xx)));
    }
    // Loop over all other wires to obtain off-diagonal elements.
    for (unsigned int j = i + 1; j < m_nWires; ++j) {
      xx = (Pi / m_sy) * (m_w[i].x - m_w[j].x);
      yy = (Pi / m_sy) * (m_w[i].y - m_w[j].y);
      if (fabs(xx) > 20.) m_a[i][j] = -fabs(xx) + CLog2;
      if (fabs(xx) <= 20.) {
        const double sinhx = sinh(xx);
        const double siny = sin(yy);
        m_a[i][j] = -0.5 * log(sinhx * sinhx + siny * siny);
      }
      // Take care of a plane at constant x.
      if (m_ynplax) {
        xxmirr = (Pi / m_sy) * (m_w[i].x + m_w[j].x - 2. * m_coplax);
        r2plan = 0.;
        if (fabs(xxmirr) > 20.) r2plan = fabs(xxmirr) - CLog2;
        if (fabs(xxmirr) <= 20.) {
          const double sinhx = sinh(xxmirr);
          const double siny = sin(yy);
          r2plan = 0.5 * log(sinhx * sinhx + siny * siny);
        }
        m_a[i][j] += r2plan;
      }
      // Copy a[i][j] to a[j][i], the capacitance matrix is symmetric.
      m_a[j][i] = m_a[i][j];
    }
  }
  // Call function CHARGE calculating all kinds of useful things.
  return Charge();
}

bool ComponentAnalyticField::SetupB2X() {

  //-----------------------------------------------------------------------
  //   SETB2X - Routine preparing the field calculations by setting the
  //            charges.
  //   VARIABLES : xx         : Difference in x of two wires * factor.
  //               yy         : Difference in y of two wires * factor.
  //               xxneg      : Difference in x of one wire and the mirror
  //                            image in period direction of another * fac.
  //               yymirr     : Difference in y of one wire and the mirror
  //                            image of another * factor.
  //-----------------------------------------------------------------------

  b2sin.resize(m_nWires);

  // Loop over all wires and calculate the diagonal elements first.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    double xx = (Pi / m_sx) * (m_w[i].x - m_coplax);
    m_a[i][i] = (0.25 * m_w[i].d * Pi / m_sx) / sin(xx);
    // Take care of a plane at constant y if it exists.
    if (m_ynplay) {
      const double yymirr = (Pi / m_sx) * (m_w[i].y - m_coplay);
      if (fabs(yymirr) <= 20.) {
        const double sinhy = sinh(yymirr);
        const double sinx = sin(xx);
        m_a[i][i] *= sqrt(sinhy * sinhy + sinx * sinx) / sinhy;
      }
    }
    // Store the true value of a[i][i].
    m_a[i][i] = -log(fabs(m_a[i][i]));
    // Loop over all other wires to obtain off-diagonal elements.
    for (unsigned int j = i + 1; j < m_nWires; ++j) {
      xx = HalfPi * (m_w[i].x - m_w[j].x) / m_sx;
      const double yy = HalfPi * (m_w[i].y - m_w[j].y) / m_sx;
      const double xxneg = HalfPi * (m_w[i].x + m_w[j].x - 2. * m_coplax) / m_sx;
      if (fabs(yy) <= 20.) {
        const double sinhy = sinh(yy);
        const double sinxx = sin(xx);
        const double sinxxneg = sin(xxneg);
        m_a[i][j] = (sinhy * sinhy + sinxx * sinxx) /
                    (sinhy * sinhy + sinxxneg * sinxxneg);
      }
      if (fabs(yy) > 20.) m_a[i][j] = 1.0;
      // Take an equipotential plane at constant y into account.
      if (m_ynplay) {
        const double yymirr =
            HalfPi * (m_w[i].y + m_w[j].y - 2. * m_coplay) / m_sx;
        if (fabs(yymirr) <= 20.) {
          const double sinhy = sinh(yymirr);
          const double sinxx = sin(xx);
          const double sinxxneg = sin(xxneg);
          m_a[i][j] *= (sinhy * sinhy + sinxxneg * sinxxneg) /
                       (sinhy * sinhy + sinxx * sinxx);
        }
      }
      // Store the true value of a[i][j] in both a[i][j] and a[j][i].
      m_a[i][j] = -0.5 * log(m_a[i][j]);
      m_a[j][i] = m_a[i][j];
    }
    // Set the b2sin vector.
    b2sin[i] = sin(Pi * (m_coplax - m_w[i].x) / m_sx);
  }
  // Call function CHARGE calculating all kinds of useful things.
  return Charge();
}

bool ComponentAnalyticField::SetupB2Y() {

  //-----------------------------------------------------------------------
  //   SETB2Y - Routine preparing the field calculations by setting the
  //            charges.
  //   VARIABLES : xx         : Difference in x of two wires * factor.
  //               yy         : Difference in y of two wires * factor.
  //               xxmirr     : Difference in x of one wire and the mirror
  //                            image of another * factor.
  //               yyneg      : Difference in y of one wire and the mirror
  //                            image in period direction of another * fac.
  //-----------------------------------------------------------------------

  b2sin.resize(m_nWires);

  // Loop over all wires and calculate the diagonal elements first.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    double yy = (Pi / m_sy) * (m_w[i].y - m_coplay);
    m_a[i][i] = (0.25 * m_w[i].d * Pi / m_sy) / sin(yy);
    // Take care of a plane at constant x if present.
    if (m_ynplax) {
      const double xxmirr = (Pi / m_sy) * (m_w[i].x - m_coplax);
      if (fabs(xxmirr) <= 20.) {
        const double sinhx = sinh(xxmirr);
        const double sinyy = sin(yy);
        m_a[i][i] *= sqrt(sinhx * sinhx + sinyy * sinyy) / sinhx;
      }
    }
    // Store the true value of a[i][i].
    m_a[i][i] = -log(fabs(m_a[i][i]));
    // Loop over all other wires to obtain off-diagonal elements.
    for (unsigned int j = i + 1; j < m_nWires; j++) {
      const double xx = HalfPi * (m_w[i].x - m_w[j].x) / m_sy;
      yy = HalfPi * (m_w[i].y - m_w[j].y) / m_sy;
      const double yyneg = HalfPi * (m_w[i].y + m_w[j].y - 2. * m_coplay) / m_sy;
      if (fabs(xx) <= 20.) {
        const double sinhx = sinh(xx);
        const double sinyy = sin(yy);
        const double sinyyneg = sin(yyneg);
        m_a[i][j] = (sinhx * sinhx + sinyy * sinyy) /
                    (sinhx * sinhx + sinyyneg * sinyyneg);
      }
      if (fabs(xx) > 20.) m_a[i][j] = 1.0;
      // Take an equipotential plane at constant x into account.
      if (m_ynplax) {
        const double xxmirr =
            HalfPi * (m_w[i].x + m_w[j].x - 2. * m_coplax) / m_sy;
        if (fabs(xxmirr) <= 20.) {
          const double sinhx = sinh(xxmirr);
          const double sinyy = sin(yy);
          const double sinyyneg = sin(yyneg);
          m_a[i][j] *= (sinhx * sinhx + sinyyneg * sinyyneg) /
                       (sinhx * sinhx + sinyy * sinyy);
        }
      }
      // Store the true value of a[i][j] in both a[i][j] and a[j][i].
      m_a[i][j] = -0.5 * log(m_a[i][j]);
      m_a[j][i] = m_a[i][j];
    }
    // Set the b2sin vector.
    b2sin[i] = sin(Pi * (m_coplay - m_w[i].y) / m_sy);
  }
  // Call function CHARGE calculating all kinds of useful things.
  return Charge();
}

bool ComponentAnalyticField::SetupC10() {
  //-----------------------------------------------------------------------
  //   SETC10 - This initialising routine computes the wire charges E and
  //            sets certain constants in common. The wire are located at
  //            (x[j],y[j])+(LX*SX,LY*SY), J=1(1)NWIRE,
  //            LX=-infinity(1)infinity, LY=-infinity(1)infinity.
  //            Use is made of the function PH2.
  //
  //  (Written by G.A.Erskine/DD, 14.8.1984 modified to some extent)
  //-----------------------------------------------------------------------

  // Initialise the constants.
  double p = 0.;
  m_p1 = m_p2 = 0.;

  mode = 0;
  if (m_sx <= m_sy) {
    mode = 1;
    if (m_sy / m_sx < 8.) p = exp(-Pi * m_sy / m_sx);
    m_zmult = std::complex<double>(Pi / m_sx, 0.);
  } else {
    mode = 0;
    if (m_sx / m_sy < 8.) p = exp(-Pi * m_sx / m_sy);
    m_zmult = std::complex<double>(0., Pi / m_sy);
  }
  m_p1 = p * p;
  if (m_p1 > 1.e-10) m_p2 = pow(p, 6);

  if (m_debug) {
    std::cout << m_className << "::SetupC10:\n";
    std::cout << "    p, p1, p2 = " << p << ", " << m_p1 << ", " << m_p2
              << "\n";
    std::cout << "    zmult = " << m_zmult << "\n";
    std::cout << "    mode = " << mode << "\n";
  }

  // Store the capacitance matrix.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    for (unsigned int j = 0; j < m_nWires; ++j) {
      const double xyi = mode == 0 ? m_w[i].x : m_w[i].y;
      const double xyj = mode == 0 ? m_w[j].x : m_w[j].y;
      const double temp = xyi * xyj * TwoPi / (m_sx * m_sy);
      if (i == j) {
        m_a[i][j] = Ph2Lim(0.5 * m_w[i].d) - temp;
      } else {
        m_a[i][j] = Ph2(m_w[i].x - m_w[j].x, m_w[i].y - m_w[j].y) - temp;
      }
    }
  }
  // Call CHARGE to find the charges.
  if (!Charge()) return false;
  // Calculate the non-logarithmic term in the potential.
  double s = 0.;
  for (unsigned int j = 0; j < m_nWires; ++j) {
    const double xyj = mode == 0 ? m_w[j].x : m_w[j].y;
    s += m_w[j].e * xyj;
  }
  m_c1 = -s * 2. * Pi / (m_sx * m_sy);
  return true;
}

bool ComponentAnalyticField::SetupC2X() {

  //-----------------------------------------------------------------------
  //   SETC2X - This initializing subroutine stores the capacitance matrix
  //            for the configuration:
  //            wires at zw(j)+cmplx(lx*2*sx,ly*sy),
  //            j=1(1)n, lx=-infinity(1)infinity, ly=-infinity(1)infinity.
  //            but the signs of the charges alternate in the x-direction
  //-----------------------------------------------------------------------

  // Initialise the constants.
  double p = 0.;
  m_p1 = m_p2 = 0.;

  mode = 0;
  if (2. * m_sx <= m_sy) {
    mode = 1;
    if (m_sy / m_sx < 25.) p = exp(-HalfPi * m_sy / m_sx);
    m_zmult = std::complex<double>(HalfPi / m_sx, 0.);
  } else {
    mode = 0;
    if (m_sx / m_sy < 6.) p = exp(-2. * Pi * m_sx / m_sy);
    m_zmult = std::complex<double>(0., Pi / m_sy);
  }
  m_p1 = p * p;
  if (m_p1 > 1.e-10) m_p2 = pow(p, 6);

  if (m_debug) {
    std::cout << m_className << "::SetupC2X:\n";
    std::cout << "    p, p1, p2 = " << p << ", " << m_p1 << ", " << m_p2
              << "\n";
    std::cout << "    zmult = " << m_zmult << "\n";
    std::cout << "    mode = " << mode << "\n";
  }

  // Fill the capacitance matrix.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    const double cx = m_coplax - m_sx * int(round((m_coplax - m_w[i].x) / m_sx));
    for (unsigned int j = 0; j < m_nWires; ++j) {
      double temp = 0.;
      if (mode == 0) {
        temp = (m_w[i].x - cx) * (m_w[j].x - cx) * TwoPi / (m_sx * m_sy);
      }
      if (i == j) {
        m_a[i][i] =
            Ph2Lim(0.5 * m_w[i].d) - Ph2(2. * (m_w[i].x - cx), 0.) - temp;
      } else {
        m_a[i][j] = Ph2(m_w[i].x - m_w[j].x, m_w[i].y - m_w[j].y) -
                    Ph2(m_w[i].x + m_w[j].x - 2. * cx, m_w[i].y - m_w[j].y) -
                    temp;
      }
    }
  }
  // Call CHARGE to find the wire charges.
  if (!Charge()) return false;
  // Determine the non-logarithmic part of the potential (0 if MODE=1).
  m_c1 = 0.;
  if (mode == 0) {
    double s = 0.;
    for (unsigned int i = 0; i < m_nWires; ++i) {
      const double cx = m_coplax - m_sx * int(round((m_coplax - m_w[i].x) / m_sx));
      s += m_w[i].e * (m_w[i].x - cx);
    }
    m_c1 = -s * TwoPi / (m_sx * m_sy);
  }
  return true;
}

bool ComponentAnalyticField::SetupC2Y() {

  //-----------------------------------------------------------------------
  //   SETC2Y - This initializing subroutine stores the capacitance matrix
  //            for the configuration:
  //            wires at zw(j)+cmplx(lx*sx,ly*2*sy),
  //            j=1(1)n, lx=-infinity(1)infinity, ly=-infinity(1)infinity.
  //            but the signs of the charges alternate in the y-direction
  //-----------------------------------------------------------------------

  // Initialise the constants.
  double p = 0.;
  m_p1 = m_p2 = 0.;

  mode = 0;
  if (m_sx <= 2. * m_sy) {
    mode = 1;
    if (m_sy / m_sx <= 6.) p = exp(-2. * Pi * m_sy / m_sx);
    m_zmult = std::complex<double>(Pi / m_sx, 0.);
  } else {
    mode = 0;
    if (m_sx / m_sy <= 25.) p = exp(-HalfPi * m_sx / m_sy);
    m_zmult = std::complex<double>(0., HalfPi / m_sy);
  }
  m_p1 = p * p;
  if (m_p1 > 1.e-10) m_p2 = pow(p, 6);

  if (m_debug) {
    std::cout << m_className << "::SetupC2Y:\n";
    std::cout << "    p, p1, p2 = " << p << ", " << m_p1 << ", " << m_p2
              << "\n";
    std::cout << "    zmult = " << m_zmult << "\n";
    std::cout << "    mode = " << mode << "\n";
  }

  // Fill the capacitance matrix.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    const double cy = m_coplay - m_sy * int(round((m_coplay - m_w[i].y) / m_sy));
    for (unsigned int j = 0; j < m_nWires; ++j) {
      double temp = 0.;
      if (mode == 1) {
        temp = (m_w[i].y - cy) * (m_w[j].y - cy) * TwoPi / (m_sx * m_sy);
      }
      if (i == j) {
        m_a[i][i] =
            Ph2Lim(0.5 * m_w[i].d) - Ph2(0., 2. * (m_w[j].y - cy)) - temp;
      } else {
        m_a[i][j] = Ph2(m_w[i].x - m_w[j].x, m_w[i].y - m_w[j].y) -
                    Ph2(m_w[i].x - m_w[j].x, m_w[i].y + m_w[j].y - 2. * cy) -
                    temp;
      }
    }
  }
  // Call CHARGE to find the wire charges.
  if (!Charge()) return false;
  // The non-logarithmic part of the potential is zero if MODE=0.
  m_c1 = 0.;
  if (mode == 1) {
    double s = 0.;
    for (unsigned int i = 0; i < m_nWires; ++i) {
      const double cy = m_coplay - m_sy * int(round((m_coplay - m_w[i].y) / m_sy));
      s += m_w[i].e * (m_w[i].y - cy);
    }
    m_c1 = -s * TwoPi / (m_sx * m_sy);
  }
  return true;
}

bool ComponentAnalyticField::SetupC30() {

  //-----------------------------------------------------------------------
  //   SETC30 - This initializing subroutine stores the capacitance matrix
  //            for a configuration with
  //            wires at zw(j)+cmplx(lx*2*sx,ly*2*sy),
  //            j=1(1)n, lx=-infinity(1)infinity, ly=-infinity(1)infinity.
  //            but the signs of the charges alternate in both directions.
  //-----------------------------------------------------------------------

  // Initialise the constants.
  double p = 0.;
  m_p1 = m_p2 = 0.;

  mode = 0;
  if (m_sx <= m_sy) {
    mode = 1;
    if (m_sy / m_sx <= 13.) p = exp(-Pi * m_sy / m_sx);
    m_zmult = std::complex<double>(HalfPi / m_sx, 0.);
  } else {
    mode = 0;
    if (m_sx / m_sy <= 13.) p = exp(-Pi * m_sx / m_sy);
    m_zmult = std::complex<double>(0., HalfPi / m_sy);
  }
  m_p1 = p * p;
  if (m_p1 > 1.e-10) m_p2 = pow(p, 6);

  if (m_debug) {
    std::cout << m_className << "::SetupC30:\n";
    std::cout << "    p, p1, p2 = " << p << ", " << m_p1 << ", " << m_p2
              << "\n";
    std::cout << "    zmult = " << m_zmult << "\n";
    std::cout << "    mode = " << mode << "\n";
  }

  // Fill the capacitance matrix.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    double cx = m_coplax - m_sx * int(round((m_coplax - m_w[i].x) / m_sx));
    double cy = m_coplay - m_sy * int(round((m_coplay - m_w[i].y) / m_sy));
    for (unsigned int j = 0; j < m_nWires; ++j) {
      if (i == j) {
        m_a[i][i] = Ph2Lim(0.5 * m_w[i].d) - Ph2(0., 2. * (m_w[i].y - cy)) -
                    Ph2(2. * (m_w[i].x - cx), 0.) +
                    Ph2(2. * (m_w[i].x - cx), 2. * (m_w[i].y - cy));
      } else {
        m_a[i][j] =
            Ph2(m_w[i].x - m_w[j].x, m_w[i].y - m_w[j].y) -
            Ph2(m_w[i].x - m_w[j].x, m_w[i].y + m_w[j].y - 2. * cy) -
            Ph2(m_w[i].x + m_w[j].x - 2. * cx, m_w[i].y - m_w[j].y) +
            Ph2(m_w[i].x + m_w[j].x - 2. * cx, m_w[i].y + m_w[j].y - 2. * cy);
      }
    }
  }
  // Call CHARGE to find the wire charges.
  if (!Charge()) return false;
  // The non-logarithmic part of the potential is zero in this case.
  m_c1 = 0.;
  return true;
}

bool ComponentAnalyticField::SetupD10() {

  //-----------------------------------------------------------------------
  //   SETD10 - Subroutine preparing the field calculations by calculating
  //            the charges on the wires, for cells with a tube.
  //
  //   (Last changed on  4/ 9/95.)
  //-----------------------------------------------------------------------

  const double r2 = m_cotube * m_cotube;
  // Loop over all wires.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Set the diagonal terms.
    m_a[i][i] = -log(0.5 * m_w[i].d * m_cotube /
                     (r2 - (m_w[i].x * m_w[i].x + m_w[i].y * m_w[i].y)));
    // Set a complex wire-coordinate to make things a little easier.
    std::complex<double> zi(m_w[i].x, m_w[i].y);
    // Loop over all other wires for the off-diagonal elements.
    for (unsigned int j = i + 1; j < m_nWires; ++j) {
      // Set a complex wire-coordinate to make things a little easier.
      std::complex<double> zj(m_w[j].x, m_w[j].y);
      m_a[i][j] = -log(abs(m_cotube * (zi - zj) / (r2 - conj(zi) * zj)));
      // Copy this to a[j][i] since the capacitance matrix is symmetric.
      m_a[j][i] = m_a[i][j];
    }
  }
  // Call CHARGE to calculate the charges really.
  return Charge();
}

bool ComponentAnalyticField::SetupD20() {

  //-----------------------------------------------------------------------
  //   SETD20 - Subroutine preparing the field calculations by calculating
  //            the charges on the wires, for cells with a tube and a
  //            phi periodicity. Assymetric capacitance matrix !
  //
  //   (Last changed on 18/ 2/93.)
  //-----------------------------------------------------------------------

  std::complex<double> zj;
  const double r2 = m_cotube * m_cotube;
  // Loop over all wires.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Set a complex wire-coordinate to make things a little easier.
    std::complex<double> zi(m_w[i].x, m_w[i].y);
    if (abs(zi) < m_w[i].d / 2.) {
      // Case of a wire near the centre.
      // Inner loop over the wires.
      for (unsigned int j = 0; j < m_nWires; ++j) {
        if (i == j) {
          // Set the diagonal terms.
          m_a[i][i] =
              -log(0.5 * m_w[i].d /
                   (m_cotube -
                    (m_w[i].x * m_w[i].x + m_w[i].y * m_w[i].y) / m_cotube));
        } else {
          // Off-diagonal terms.
          std::complex<double> zj(m_w[j].x, m_w[j].y);
          m_a[j][i] = -log(abs((1. / m_cotube) * (zi - zj) /
                               (1. - conj(zi) * zj / r2)));
        }
      }
    } else {
      // Normal case.
      // Inner wire loop.
      for (unsigned int j = 0; j < m_nWires; ++j) {
        if (i == j) {
          // Diagonal elements.
          m_a[i][i] =
              -log(abs(0.5 * m_w[i].d * m_mtube * pow(zi, m_mtube - 1) /
                       (pow(m_cotube, m_mtube) *
                        (1. - pow((abs(zi) / m_cotube), 2 * m_mtube)))));
        } else {
          // Off-diagonal terms.
          std::complex<double> zj(m_w[j].x, m_w[j].y);
          m_a[j][i] = -log(abs((1 / pow(m_cotube, m_mtube)) *
                               (pow(zj, m_mtube) - pow(zi, m_mtube)) /
                               (1. - pow(zj * conj(zi) / r2, m_mtube))));
        }
      }
    }
  }
  // Call CHARGE to calculate the charges really.
  return Charge();
}

bool ComponentAnalyticField::SetupD30() {

  //-----------------------------------------------------------------------
  //   SETD30 - Subroutine preparing the field calculations by calculating
  //            the charges on the wires, for cells with wires inside a
  //            polygon.
  //
  //   (Last changed on 21/ 2/94.)
  //-----------------------------------------------------------------------

  wmap.assign(m_nWires, std::complex<double>(0., 0.));

  std::complex<double> wd = std::complex<double>(0., 0.);

  InitializeCoefficientTables();

  // Evaluate kappa, a constant needed by ConformalMap.
  m_kappa = tgamma((m_ntube + 1.) / m_ntube) *
            tgamma((m_ntube - 2.) / m_ntube) / tgamma((m_ntube - 1.) / m_ntube);
  // Loop over all wire combinations.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Compute wire mappings only once.
    ConformalMap(std::complex<double>(m_w[i].x, m_w[i].y) / m_cotube, wmap[i],
                 wd);
    // Diagonal elements.
    m_a[i][i] = -log(abs((0.5 * m_w[i].d / m_cotube) * wd /
                         (1. - pow(abs(wmap[i]), 2))));
    // Loop over all other wires for the off-diagonal elements.
    for (unsigned int j = 0; j < i; ++j) {
      m_a[i][j] =
          -log(abs((wmap[i] - wmap[j]) / (1. - conj(wmap[i]) * wmap[j])));
      // Copy this to a[j][i] since the capacitance matrix is symmetric.
      m_a[j][i] = m_a[i][j];
    }
  }
  // Call CHARGE to calculate the charges really.
  return Charge();
}

bool ComponentAnalyticField::Charge() {

  //-----------------------------------------------------------------------
  //   CHARGE - Routine actually inverting the capacitance matrix filled in
  //            the SET... routines thereby providing the charges.
  //   (Last changed on 30/ 1/93.)
  //-----------------------------------------------------------------------

  // Transfer the voltages to rhs vector,
  // correcting for the equipotential planes.
  std::vector<double> b(m_nWires, 0.);
  for (unsigned int i = 0; i < m_nWires; ++i) {
    b[i] = m_w[i].v - (m_corvta * m_w[i].x + m_corvtb * m_w[i].y + m_corvtc);
  }

  bool ok = true;
  int ifail = 0;

  // Force sum charges = 0 in case of absence of equipotential planes.
  if (!(m_ynplan[0] || m_ynplan[1] || m_ynplan[2] || m_ynplan[3] || m_tube)) {
    // Add extra elements to A, acting as constraints.
    b.push_back(0.);
    m_a.resize(m_nWires + 1);
    m_a[m_nWires].clear();
    for (unsigned int i = 0; i < m_nWires; ++i) {
      m_a[i].push_back(1.);
      m_a[m_nWires].push_back(1.);
    }
    m_a[m_nWires].push_back(0.);
    // Solve equations to yield charges, using KERNLIB (scalar).
    Numerics::Deqinv(m_nWires + 1, m_a, ifail, b);
    if (ifail != 0) {
      std::cerr << m_className << "::Charge:\n";
      std::cerr << "    Matrix inversion failed.\n";
      return false;
    }
    // Modify A to give true inverse of capacitance matrix.
    if (m_a[m_nWires][m_nWires] != 0.) {
      const double t = 1. / m_a[m_nWires][m_nWires];
      for (unsigned int i = 0; i < m_nWires; ++i) {
        for (unsigned int j = 0; j < m_nWires; ++j) {
          m_a[i][j] -= t * m_a[i][m_nWires] * m_a[m_nWires][j];
        }
      }
    } else {
      std::cerr << m_className << "::Charge:\n";
      std::cerr << "    True inverse of the capacitance matrix"
                << " could not be calculated.\n";
      std::cerr << "    Use of the FACTOR instruction should be avoided.\n";
      ok = false;
    }
    // Store reference potential.
    m_v0 = b[m_nWires];
  } else {
    // Handle the case when the sum of the charges is zero automatically.
    Numerics::Deqinv(m_nWires, m_a, ifail, b);
    // Reference potential chosen to be zero.
    m_v0 = 0.;
  }

  // Check the error condition flag.
  if (!ok) {
    std::cerr << m_className << "::Charge:\n";
    std::cerr << "    Failure to solve the capacitance equations.\n";
    std::cerr << "    No charges are available.\n";
    return false;
  }

  // Copy the charges to E.
  for (unsigned int i = 0; i < m_nWires; ++i) m_w[i].e = b[i];

  // If debugging is on, print the capacitance matrix.
  if (m_debug) {
    std::cout << m_className << "::Charge:\n";
    std::cout << "    Dump of the capacitance matrix after inversion:\n";
    for (unsigned int i = 0; i < m_nWires; i += 10) {
      for (unsigned int j = 0; j < m_nWires; j += 10) {
        std::cout << "    (Block " << i / 10 << ", " << j / 10 << ")\n";
        for (unsigned int ii = 0; ii < 10; ++ii) {
          if (i + ii >= m_nWires) break;
          for (unsigned int jj = 0; jj < 10; ++jj) {
            if (j + jj >= m_nWires) break;
            std::cout << std::setw(6) << m_a[i + ii][j + jj] << " ";
          }
          std::cout << "\n";
        }
        std::cout << "\n";
      }
    }
    std::cout << m_className << "::Charge:\n";
    std::cout << "    End of the inverted capacitance matrix.\n";
  }

  // And also check the quality of the matrix inversion.
  if (m_chargeCheck) {
    std::cout << m_className << "::Charge:\n";
    std::cout << "    Quality check of the charge calculation.\n";
    std::cout << "    Wire       E as obtained        E reconstructed\n";
    for (unsigned int i = 0; i < m_nWires; ++i) {
      b[i] = 0.;
      for (unsigned int j = 0; j < m_nWires; ++j) {
        b[i] += m_a[i][j] * (m_w[j].v - m_v0 -
                             (m_corvta * m_w[j].x + m_corvtb * m_w[j].y + m_corvtc));
      }
      std::cout << "    " << i << "      " << m_w[i].e << "    " << b[i]
                << "\n";
    }
  }
  return true;
}

double ComponentAnalyticField::Ph2(const double xpos, const double ypos) const {

  //-----------------------------------------------------------------------
  //   PH2    - Logarithmic contribution to real single-wire potential,
  //            for a doubly priodic wire array.
  //   PH2LIM - Entry, PH2LIM(r) corresponds to z on the surface of a wire
  //            of (small) radius r.
  //
  //            Clenshaw's algorithm is used for the evaluation of the sum
  //            ZTERM = SIN(zeta) - P1*SIN(3*zeta) + P2*SIN(5*zeta).
  //
  //  (G.A.Erskine/DD, 14.8.1984; some minor modifications (i) common block
  //   /EV2COM/ incorporated in /CELDAT/ (ii) large imag(zeta) corrected)
  //-----------------------------------------------------------------------

  // Start of the main subroutine, off diagonal elements.
  std::complex<double> zeta = m_zmult * std::complex<double>(xpos, ypos);
  if (fabs(imag(zeta)) < 10.) {
    std::complex<double> zsin = sin(zeta);
    std::complex<double> zcof = 4. * zsin * zsin - 2.;
    std::complex<double> zu = -m_p1 - zcof * m_p2;
    std::complex<double> zunew = 1. - zcof * zu - m_p2;
    std::complex<double> zterm = (zunew + zu) * zsin;
    return -log(abs(zterm));
  }

  return -fabs(imag(zeta)) + CLog2;
}

void ComponentAnalyticField::ConformalMap(const std::complex<double>& z,
                                          std::complex<double>& ww,
                                          std::complex<double>& wd) const {

  //-----------------------------------------------------------------------
  //   EFCMAP - Maps a the interior part of a regular in the unit circle.
  //   Variables: Z     - point to be mapped
  //              W     - the image of Z
  //              WD    - derivative of the mapping at Z
  //              CC1   - coefficients for expansion around centre
  //              CC2   - coefficients for expansion around corner
  //   (Last changed on 19/ 2/94.)
  //-----------------------------------------------------------------------

  const int nterm = 15;

  if (z == 0.) {
    // Z coincides with the centre.
    // Results are trivial.
    ww = 0;
    wd = m_kappa;
  } else if (abs(z) < 0.75) {
    // Z is close to the centre.
    // Series expansion.
    std::complex<double> zterm = pow(m_kappa * z, m_ntube);
    std::complex<double> wdsum = 0.;
    std::complex<double> wsum = m_cc1[m_ntube - 3][nterm];
    for (int i = nterm; i--;) {
      wdsum = wsum + zterm * wdsum;
      wsum = m_cc1[m_ntube - 3][i] + zterm * wsum;
    }
    // Return the results.
    ww = m_kappa * z * wsum;
    wd = m_kappa * (wsum + double(m_ntube) * zterm * wdsum);
  } else {
    // Z is close to the edge.
    // First rotate Z nearest to 1.
    double arot = -TwoPi *
                  int(round(atan2(imag(z), real(z)) * m_ntube / TwoPi)) /
                  m_ntube;
    std::complex<double> zz = z * std::complex<double>(cos(arot), sin(arot));
    // Expand in a series.
    std::complex<double> zterm =
        pow(m_kappa * (1. - zz), m_ntube / (m_ntube - 2.));
    std::complex<double> wdsum = 0.;
    std::complex<double> wsum = m_cc2[m_ntube - 3][nterm];
    for (int i = nterm; i--;) {
      wdsum = wsum + zterm * wdsum;
      wsum = m_cc2[m_ntube - 3][i] + zterm * wsum;
    }
    // And return the results.
    ww = std::complex<double>(cos(arot), -sin(arot)) * (1. - zterm * wsum);
    wd = m_ntube * m_kappa * pow(m_kappa * (1. - zz), 2. / (m_ntube - 2.)) *
         (wsum + zterm * wdsum) / (m_ntube - 2.);
  }
}

void ComponentAnalyticField::E2Sum(const double xpos, const double ypos,
                                   double& ex, double& ey) const {

  //-----------------------------------------------------------------------
  //   E2SUM  - Components of the elecrostatic field intensity in a doubly
  //            periodic wire array.
  //            Clenshaw's algorithm is used for the evaluation of the sums
  //                  ZTERM1 = SIN(ZETA) - P1*SIN(3*ZETA) + P2*SIN(5*ZETA),
  //                  ZTERM2 = COS(ZETA)- 3 P1*COS(3*ZETA)+ 5P2*COS(5*ZETA)
  //   VARIABLES : (XPOS,YPOS): Position in the basic cell at which the
  //                            field is to be computed.
  //  (Essentially by G.A.Erskine/DD, 14.8.1984)
  //-----------------------------------------------------------------------

  const std::complex<double> icons = std::complex<double>(0., 1.);

  std::complex<double> wsum = 0.;
  std::complex<double> zsin, zcof, zu, zunew;
  std::complex<double> zterm1, zterm2, zeta;

  for (unsigned int j = 0; j < m_nWires; ++j) {
    zeta = m_zmult * std::complex<double>(xpos - m_w[j].x, ypos - m_w[j].y);
    if (imag(zeta) > 15.) {
      wsum -= m_w[j].e * icons;
    } else if (imag(zeta) < -15.) {
      wsum += m_w[j].e * icons;
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum += m_w[j].e * (zterm2 / zterm1);
    }
  }
  ex = -real(-m_zmult * wsum);
  ey = imag(-m_zmult * wsum);
}

void ComponentAnalyticField::FieldA00(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCA00 - Subroutine performing the actual field calculations in case
  //            only one charge and not more than 1 mirror-charge in either
  //            x or y is present.
  //            The potential used is 1/2*pi*eps0  log(r).
  //   VARIABLES : R2         : Potential before taking -log(sqrt(...))
  //               EX, EY     : x,y-component of the electric field.
  //               ETOT       : Magnitude of electric field.
  //               VOLT       : Potential.
  //               EXHELP etc : One term in the series to be summed.
  //               (XPOS,YPOS): The position where the field is calculated.
  //   (Last changed on 25/ 1/96.)
  //-----------------------------------------------------------------------

  // Initialise the electric field and potential.
  ex = ey = 0.;
  volt = m_v0;

  double xxmirr = 0., yymirr = 0.;
  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    const double xx = xpos - m_w[i].x;
    const double yy = ypos - m_w[i].y;
    double r2 = xx * xx + yy * yy;
    // Calculate the field in case there are no planes.
    double exhelp = xx / r2;
    double eyhelp = yy / r2;
    // Take care of a plane at constant x.
    if (m_ynplax) {
      xxmirr = m_w[i].x + (xpos - 2. * m_coplax);
      const double r2plan = xxmirr * xxmirr + yy * yy;
      exhelp -= xxmirr / r2plan;
      eyhelp -= yy / r2plan;
      r2 /= r2plan;
    }
    // Take care of a plane at constant y.
    if (m_ynplay) {
      yymirr = m_w[i].y + (ypos - 2. * m_coplay);
      const double r2plan = xx * xx + yymirr * yymirr;
      exhelp -= xx / r2plan;
      eyhelp -= yymirr / r2plan;
      r2 /= r2plan;
    }
    // Take care of pairs of planes.
    if (m_ynplax && m_ynplay) {
      const double r2plan = xxmirr * xxmirr + yymirr * yymirr;
      exhelp += xxmirr / r2plan;
      eyhelp += yymirr / r2plan;
      r2 *= r2plan;
    }
    // Calculate the electric field and potential.
    if (opt) volt -= 0.5 * m_w[i].e * log(r2);
    ex += m_w[i].e * exhelp;
    ey += m_w[i].e * eyhelp;
  }
}

void ComponentAnalyticField::FieldB1X(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCB1X - Routine calculating the potential for a row of positive
  //            charges. The potential used is Re(Log(sin pi/s (z-z0))).
  //   VARIABLES : See routine EFCA00 for most of the variables.
  //               Z,ZZMIRR   : X + I*Y , XXMIRR + I*YYMIRR ; I**2=-1
  //               ECOMPL     : EX + I*EY                   ; I**2=-1
  //-----------------------------------------------------------------------

  const std::complex<double> icons = std::complex<double>(0., 1.);

  std::complex<double> ecompl;

  double r2 = 0.;

  // Initialise the electric field and potential.
  ex = ey = 0.;
  volt = m_v0;

  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    const double xx = (Pi / m_sx) * (xpos - m_w[i].x);
    const double yy = (Pi / m_sx) * (ypos - m_w[i].y);
    std::complex<double> zz(xx, yy);
    // Calculate the field in case there are no equipotential planes.
    if (yy > 20.) ecompl = -icons;
    if (fabs(yy) <= 20.)
      ecompl =
          icons * (exp(2. * icons * zz) + 1.) / (exp(2. * icons * zz) - 1.);
    if (yy < -20.) ecompl = icons;
    if (opt) {
      if (fabs(yy) > 20.) r2 = -fabs(yy) + CLog2;
      if (fabs(yy) <= 20.) r2 = -0.5 * log(pow(sinh(yy), 2) + pow(sin(xx), 2));
    }
    // Take care of a plane at constant y.
    if (m_ynplay) {
      const double yymirr = (Pi / m_sx) * (ypos + m_w[i].y - 2. * m_coplay);
      std::complex<double> zzmirr(xx, yymirr);
      if (yymirr > 20.) ecompl += icons;
      if (fabs(yymirr) <= 20.)
        ecompl += -icons * (exp(2. * icons * zzmirr) + 1.) /
                  (exp(2. * icons * zzmirr) - 1.);
      if (yymirr < -20.) ecompl += -icons;

      if (opt && fabs(yymirr) > 20.) r2 += fabs(yymirr) - CLog2;
      if (opt && fabs(yymirr) <= 20.)
        r2 += 0.5 * log(pow(sinh(yymirr), 2) + pow(sin(xx), 2));
    }
    // Calculate the electric field and potential.
    ex += m_w[i].e * real(ecompl);
    ey -= m_w[i].e * imag(ecompl);
    if (opt) volt += m_w[i].e * r2;
  }
  ex *= Pi / m_sx;
  ey *= Pi / m_sx;
}

void ComponentAnalyticField::FieldB1Y(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCB1Y - Routine calculating the potential for a row of positive
  //            charges. The potential used is Re(Log(sinh pi/sy(z-z0)).
  //   VARIABLES : See routine EFCA00 for most of the variables.
  //               Z,ZZMIRR   : X + I*Y , XXMIRR + I*YYMIRR ; I**2=-1
  //               ECOMPL     : EX + I*EY                   ; I**2=-1
  //-----------------------------------------------------------------------

  std::complex<double> ecompl;

  double r2 = 0.;

  // Initialise the electric field and potential.
  ex = ey = 0.;
  volt = m_v0;

  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    const double xx = (Pi / m_sy) * (xpos - m_w[i].x);
    const double yy = (Pi / m_sy) * (ypos - m_w[i].y);
    const std::complex<double> zz(xx, yy);
    // Calculate the field in case there are no equipotential planes.
    if (xx > 20.) ecompl = 1.;
    if (fabs(xx) <= 20.) ecompl = (exp(2. * zz) + 1.) / (exp(2. * zz) - 1.);
    if (xx < -20.) ecompl = -1.;
    if (opt) {
      if (fabs(xx) > 20.) r2 = -fabs(xx) + CLog2;
      if (fabs(xx) <= 20.) r2 = -0.5 * log(pow(sinh(xx), 2) + pow(sin(yy), 2));
    }
    // Take care of a plane at constant x.
    if (m_ynplax) {
      const double xxmirr = (Pi / m_sy) * (xpos + m_w[i].x - 2. * m_coplax);
      const std::complex<double> zzmirr(xxmirr, yy);
      if (xxmirr > 20.) ecompl -= 1.;
      if (xxmirr < -20.) ecompl += 1.;
      if (fabs(xxmirr) <= 20.)
        ecompl -= (exp(2. * zzmirr) + 1.) / (exp(2. * zzmirr) - 1.);
      if (opt && fabs(xxmirr) > 20.) r2 += fabs(xxmirr) - CLog2;
      if (opt && fabs(xxmirr) <= 20.)
        r2 += 0.5 * log(pow(sinh(xxmirr), 2) + pow(sin(yy), 2));
    }
    // Calculate the electric field and potential.
    ex += m_w[i].e * real(ecompl);
    ey -= m_w[i].e * imag(ecompl);
    if (opt) volt += m_w[i].e * r2;
  }
  ex *= Pi / m_sy;
  ey *= Pi / m_sy;
}

void ComponentAnalyticField::FieldB2X(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCB2X - Routine calculating the potential for a row of alternating
  //            + - charges. The potential used is re log(sin pi/sx (z-z0))
  //   VARIABLES : See routine EFCA00 for most of the variables.
  //               Z, ZZMRR   : X + i*Y , XXMIRR + i*YYMIRR ; i**2=-1
  //               ECOMPL     : EX + i*EY                   ; i**2=-1
  //   (Cray vectorisable)
  //-----------------------------------------------------------------------

  std::complex<double> ecompl;

  // Initialise the electric field and potential.
  ex = ey = 0.;
  volt = m_v0;

  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    const double xx = HalfPi * (xpos - m_w[i].x) / m_sx;
    const double yy = HalfPi * (ypos - m_w[i].y) / m_sx;
    const double xxneg = HalfPi * (xpos - m_w[i].x - 2 * m_coplax) / m_sx;
    const std::complex<double> zz(xx, yy);
    const std::complex<double> zzneg(xxneg, yy);
    // Calculate the field in case there are no equipotential planes.
    ecompl = 0.;
    double r2 = 1.;
    if (fabs(yy) <= 20.) {
      ecompl = -b2sin[i] / (sin(zz) * sin(zzneg));
      if (opt) {
        const double sinhy = sinh(yy);
        const double sinxx = sin(xx);
        const double sinxxneg = sin(xxneg);
        r2 = (sinhy * sinhy + sinxx * sinxx) /
             (sinhy * sinhy + sinxxneg * sinxxneg);
      }
    }
    // Take care of a planes at constant y.
    if (m_ynplay) {
      const double yymirr = HalfPi * (ypos + m_w[i].y - 2 * m_coplay) / m_sx;
      const std::complex<double> zzmirr(xx, yymirr);
      const std::complex<double> zznmirr(xxneg, yymirr);
      if (fabs(yymirr) <= 20.) {
        ecompl += b2sin[i] / (sin(zzmirr) * sin(zznmirr));
        if (opt) {
          const double sinhy = sinh(yymirr);
          const double sinxx = sin(xx);
          const double sinxxneg = sin(xxneg);
          const double r2plan = (sinhy * sinhy + sinxx * sinxx) /
                                (sinhy * sinhy + sinxxneg * sinxxneg);
          r2 /= r2plan;
        }
      }
    }
    // Calculate the electric field and potential.
    ex += m_w[i].e * real(ecompl);
    ey -= m_w[i].e * imag(ecompl);
    if (opt) volt -= 0.5 * m_w[i].e * log(r2);
  }
  ex *= (HalfPi / m_sx);
  ey *= (HalfPi / m_sx);
}

void ComponentAnalyticField::FieldB2Y(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCB2Y - Routine calculating the potential for a row of alternating
  //            + - charges. The potential used is re log(sin pi/sx (z-z0))
  //   VARIABLES : See routine EFCA00 for most of the variables.
  //               Z, ZMIRR   : X + i*Y , XXMIRR + i*YYMIRR ; i**2=-1
  //               ECOMPL     : EX + i*EY                   ; i**2=-1
  //   (Cray vectorisable)
  //-----------------------------------------------------------------------

  const std::complex<double> icons(0., 1.);

  std::complex<double> zzmirr, zznmirr, ecompl;

  // Initialise the electric field and potential.
  ex = ey = 0.;
  volt = m_v0;

  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    const double xx = HalfPi * (xpos - m_w[i].x) / m_sy;
    const double yy = HalfPi * (ypos - m_w[i].y) / m_sy;
    const double yyneg = HalfPi * (ypos + m_w[i].y - 2. * m_coplay) / m_sy;
    const std::complex<double> zz(xx, yy);
    const std::complex<double> zzneg(xx, yyneg);
    // Calculate the field in case there are no equipotential planes.
    ecompl = 0.;
    double r2 = 1.;
    if (fabs(xx) <= 20.) {
      ecompl = icons * b2sin[i] / (sin(icons * zz) * sin(icons * zzneg));
      if (opt) {
        const double sinhx = sinh(xx);
        const double sinyy = sin(yy);
        const double sinyyneg = sin(yyneg);
        r2 = (sinhx * sinhx + sinyy * sinyy) /
             (sinhx * sinhx + sinyyneg * sinyyneg);
      }
    }
    // Take care of a plane at constant x.
    if (m_ynplax) {
      const double xxmirr = HalfPi * (xpos + m_w[i].x - 2. * m_coplax) / m_sy;
      const std::complex<double> zzmirr(xxmirr, yy);
      const std::complex<double> zznmirr(xxmirr, yyneg);
      if (fabs(xxmirr) <= 20.) {
        ecompl -=
            icons * b2sin[i] / (sin(icons * zzmirr) * sin(icons * zznmirr));
        if (opt) {
          const double sinhx = sinh(xxmirr);
          const double sinyy = sin(yy);
          const double sinyyneg = sin(yyneg);
          const double r2plan = (sinhx * sinhx + sinyy * sinyy) /
                                (sinhx * sinhx + sinyyneg * sinyyneg);
          r2 /= r2plan;
        }
      }
    }
    // Calculate the electric field and potential.
    ex += m_w[i].e * real(ecompl);
    ey -= m_w[i].e * imag(ecompl);
    if (opt) volt -= 0.5 * m_w[i].e * log(r2);
  }
  ex *= (HalfPi / m_sy);
  ey *= (HalfPi / m_sy);
}

void ComponentAnalyticField::FieldC10(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCC10 - Routine returning the potential and electric field. It
  //            calls the routines PH2 and E2SUM written by G.A.Erskine.
  //   VARIABLES : No local variables.
  //-----------------------------------------------------------------------

  // Calculate voltage first, if needed.
  if (opt) {
    if (mode == 0) volt = m_v0 + m_c1 * xpos;
    if (mode == 1) volt = m_v0 + m_c1 * ypos;
    for (unsigned int i = 0; i < m_nWires; ++i) {
      volt += m_w[i].e * Ph2(xpos - m_w[i].x, ypos - m_w[i].y);
    }
  }

  // And finally the electric field.
  E2Sum(xpos, ypos, ex, ey);
  if (mode == 0) ex -= m_c1;
  if (mode == 1) ey -= m_c1;
}

void ComponentAnalyticField::FieldC2X(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCC2X - Routine returning the potential and electric field in a
  //            configuration with 2 x planes and y periodicity.
  //   VARIABLES : see the writeup
  //-----------------------------------------------------------------------

  const std::complex<double> icons(0., 1.);

  std::complex<double> zsin, zcof, zu, zunew;
  std::complex<double> zterm1, zterm2, zeta;

  // Initial values.
  std::complex<double> wsum1 = 0.;
  std::complex<double> wsum2 = 0.;
  volt = 0.;

  // Wire loop.
  for (int i = m_nWires; i--;) {
    // Compute the direct contribution.
    zeta = m_zmult * std::complex<double>(xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum1 -= m_w[i].e * icons;
      if (opt) volt -= m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum1 += m_w[i].e * icons;
      if (opt) volt -= m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum1 += m_w[i].e * (zterm2 / zterm1);
      if (opt) volt -= m_w[i].e * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    double cx = m_coplax - m_sx * int(round((m_coplax - m_w[i].x) / m_sx));
    // Mirror contribution.
    zeta = m_zmult *
           std::complex<double>(2. * cx - xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum2 -= m_w[i].e * icons;
      if (opt) volt += m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum2 += m_w[i].e * icons;
      if (opt) volt += m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum2 += m_w[i].e * (zterm2 / zterm1);
      if (opt) volt += m_w[i].e * log(abs(zterm1));
    }
    // Correct the voltage, if needed (MODE).
    if (opt && mode == 0) {
      volt -= TwoPi * m_w[i].e * (xpos - cx) * (m_w[i].x - cx) / (m_sx * m_sy);
    }
  }
  // Convert the two contributions to a real field.
  ex = real(m_zmult * (wsum1 + wsum2));
  ey = -imag(m_zmult * (wsum1 - wsum2));
  // Constant correction terms.
  if (mode == 0) ex -= m_c1;
}

void ComponentAnalyticField::FieldC2Y(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCC2Y - Routine returning the potential and electric field in a
  //            configuration with 2 y planes and x periodicity.
  //   VARIABLES : see the writeup
  //-----------------------------------------------------------------------

  const std::complex<double> icons(0., 1.);

  std::complex<double> zsin, zcof, zu, zunew;
  std::complex<double> zterm1, zterm2, zeta;

  // Initial values.
  volt = 0.;
  std::complex<double> wsum1 = 0.;
  std::complex<double> wsum2 = 0.;

  // Wire loop.
  for (int i = m_nWires; i--;) {
    // Compute the direct contribution.
    zeta = m_zmult * std::complex<double>(xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum1 -= m_w[i].e * icons;
      if (opt) volt -= m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum1 += m_w[i].e * icons;
      if (opt) volt -= m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum1 += m_w[i].e * (zterm2 / zterm1);
      if (opt) volt -= m_w[i].e * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    const double cy = m_coplay - m_sy * int(round((m_coplay - m_w[i].y) / m_sy));
    // Mirror contribution from the y plane.
    zeta = m_zmult *
           std::complex<double>(xpos - m_w[i].x, 2 * cy - ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum2 -= m_w[i].e * icons;
      if (opt) volt += m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum2 += m_w[i].e * icons;
      if (opt) volt += m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum2 += m_w[i].e * (zterm2 / zterm1);
      if (opt) volt += m_w[i].e * log(abs(zterm1));
    }
    // Correct the voltage, if needed (MODE).
    if (opt && mode == 1) {
      volt -= TwoPi * m_w[i].e * (ypos - cy) * (m_w[i].y - cy) / (m_sx * m_sy);
    }
  }
  // Convert the two contributions to a real field.
  ex = real(m_zmult * (wsum1 - wsum2));
  ey = -imag(m_zmult * (wsum1 + wsum2));
  // Constant correction terms.
  if (mode == 1) ey -= m_c1;
}

void ComponentAnalyticField::FieldC30(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCC30 - Routine returning the potential and electric field in a
  //            configuration with 2 y and 2 x planes.
  //   VARIABLES : see the writeup
  //-----------------------------------------------------------------------

  const std::complex<double> icons(0., 1.);

  std::complex<double> zsin, zcof, zu, zunew;
  std::complex<double> zterm1, zterm2, zeta;

  // Initial values.
  std::complex<double> wsum1 = 0.;
  std::complex<double> wsum2 = 0.;
  std::complex<double> wsum3 = 0.;
  std::complex<double> wsum4 = 0.;
  volt = 0.;

  // Wire loop.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Compute the direct contribution.
    zeta = m_zmult * std::complex<double>(xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum1 -= m_w[i].e * icons;
      if (opt) volt -= m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum1 += m_w[i].e * icons;
      if (opt) volt -= m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum1 += m_w[i].e * (zterm2 / zterm1);
      if (opt) volt -= m_w[i].e * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    const double cx = m_coplax - m_sx * int(round((m_coplax - m_w[i].x) / m_sx));
    // Mirror contribution from the x plane.
    zeta = m_zmult *
           std::complex<double>(2. * cx - xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum2 -= m_w[i].e * icons;
      if (opt) volt += m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum2 += m_w[i].e * icons;
      if (opt) volt += m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum2 += m_w[i].e * (zterm2 / zterm1);
      if (opt) volt += m_w[i].e * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    const double cy = m_coplay - m_sy * int(round((m_coplay - m_w[i].y) / m_sy));
    // Mirror contribution from the x plane.
    zeta = m_zmult *
           std::complex<double>(xpos - m_w[i].x, 2. * cy - ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum3 -= m_w[i].e * icons;
      if (opt) volt += m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum3 += m_w[i].e * icons;
      if (opt) volt += m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum3 += m_w[i].e * (zterm2 / zterm1);
      if (opt) volt += m_w[i].e * log(abs(zterm1));
    }
    // Mirror contribution from both the x and the y plane.
    zeta = m_zmult * std::complex<double>(2. * cx - xpos - m_w[i].x,
                                          2. * cy - ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum4 -= m_w[i].e * icons;
      if (opt) volt -= m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum4 += m_w[i].e * icons;
      if (opt) volt -= m_w[i].e * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum4 += m_w[i].e * (zterm2 / zterm1);
      if (opt) volt -= m_w[i].e * log(abs(zterm1));
    }
  }
  // Convert the two contributions to a real field.
  ex = real(m_zmult * (wsum1 + wsum2 - wsum3 - wsum4));
  ey = -imag(m_zmult * (wsum1 - wsum2 + wsum3 - wsum4));
}

void ComponentAnalyticField::FieldD10(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCD10 - Subroutine performing the actual field calculations for a
  //            cell which has a one circular plane and some wires.
  //   VARIABLES : EX, EY, VOLT:Electric field and potential.
  //               ETOT, VOLT : Magnitude of electric field, potential.
  //               (XPOS,YPOS): The position where the field is calculated.
  //               ZI, ZPOS   : Shorthand complex notations.
  //   (Last changed on  4/ 9/95.)
  //-----------------------------------------------------------------------

  // Initialise the electric field and potential.
  ex = ey = 0.;
  volt = m_v0;

  // Set the complex position coordinates.
  const std::complex<double> zpos = std::complex<double>(xpos, ypos);
  const double r2 = m_cotube * m_cotube;
  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    // Set the complex version of the wire-coordinate for simplicity.
    const std::complex<double> zi(m_w[i].x, m_w[i].y);
    // Compute the contribution to the potential, if needed.
    if (opt)
      volt -=
          m_w[i].e * log(abs(m_cotube * (zpos - zi) / (r2 - zpos * conj(zi))));
    // Compute the contribution to the electric field, always.
    const std::complex<double> wi =
        1. / conj(zpos - zi) + zi / (r2 - conj(zpos) * zi);
    ex += m_w[i].e * real(wi);
    ey += m_w[i].e * imag(wi);
  }
}

void ComponentAnalyticField::FieldD20(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCD20 - Subroutine performing the actual field calculations for a
  //            cell which has a tube and phi periodicity.
  //   VARIABLES : EX, EY, VOLT:Electric field and potential.
  //               ETOT, VOLT : Magnitude of electric field, potential.
  //               (XPOS,YPOS): The position where the field is calculated.
  //               ZI, ZPOS   : Shorthand complex notations.
  //   (Last changed on 10/ 2/93.)
  //-----------------------------------------------------------------------

  // Initialise the electric field and potential.
  ex = ey = 0.;
  volt = m_v0;

  // Set the complex position coordinates.
  const std::complex<double> zpos = std::complex<double>(xpos, ypos);
  std::complex<double> wi;
  const double r2 = m_cotube * m_cotube;
  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    // Set the complex version of the wire-coordinate for simplicity.
    const std::complex<double> zi(m_w[i].x, m_w[i].y);
    // Case of the wire which is not in the centre.
    if (abs(zi) > m_w[i].d / 2.) {
      // Compute the contribution to the potential, if needed.
      if (opt) {
        volt -= m_w[i].e * log(abs((1. / pow(m_cotube, m_mtube)) *
                                   (pow(zpos, m_mtube) - pow(zi, m_mtube)) /
                                   (1. - pow(zpos * conj(zi) / r2, m_mtube))));
      }
      // Compute the contribution to the electric field, always.
      const std::complex<double> wi =
          double(m_mtube) * pow(conj(zpos), m_mtube - 1) *
          (1. / conj(pow(zpos, m_mtube) - pow(zi, m_mtube)) +
           pow(zi, m_mtube) /
               (pow(m_cotube, 2 * m_mtube) - pow(conj(zpos) * zi, m_mtube)));
      ex += m_w[i].e * real(wi);
      ey += m_w[i].e * imag(wi);
    } else {
      // Case of the central wire.
      if (opt) {
        volt -= m_w[i].e * log(abs((1. / m_cotube) * (zpos - zi) /
                                   (1. - zpos * conj(zi) / r2)));
      }
      const std::complex<double> wi =
          1. / conj(zpos - zi) + zi / (r2 - conj(zpos) * zi);
      // Compute the contribution to the electric field, always.
      ex += m_w[i].e * real(wi);
      ey += m_w[i].e * imag(wi);
    }
  }
}

void ComponentAnalyticField::FieldD30(const double xpos, const double ypos,
                                      double& ex, double& ey, double& volt,
                                      const bool opt) const {

  //-----------------------------------------------------------------------
  //   EFCD30 - Subroutine performing the actual field calculations for a
  //            cell which has a polygon as tube and some wires.
  //   VARIABLES : EX, EY, VOLT:Electric field and potential.
  //               ETOT, VOLT : Magnitude of electric field, potential.
  //               (XPOS,YPOS): The position where the field is calculated.
  //               ZI, ZPOS   : Shorthand complex notations.
  //   (Last changed on 19/ 2/94.)
  //-----------------------------------------------------------------------

  // Initialise the electric field and potential.
  ex = ey = 0.;
  volt = m_v0;

  std::complex<double> whelp;

  // Get the mapping of the position.
  std::complex<double> wpos, wdpos;
  ConformalMap(std::complex<double>(xpos, ypos) / m_cotube, wpos, wdpos);
  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    // Compute the contribution to the potential, if needed.
    if (opt) {
      volt -=
          m_w[i].e * log(abs((wpos - wmap[i]) / (1. - wpos * conj(wmap[i]))));
    }
    whelp = wdpos * (1. - pow(abs(wmap[i]), 2)) /
            ((wpos - wmap[i]) * (1. - conj(wmap[i]) * wpos));
    // Compute the contribution to the electric field, always.
    ex += m_w[i].e * real(whelp);
    ey -= m_w[i].e * imag(whelp);
  }
  ex /= m_cotube;
  ey /= m_cotube;
}

bool ComponentAnalyticField::InTube(const double x0, const double y0,
                                    const double a, const int n) const {

  //-----------------------------------------------------------------------
  //   INTUBE - Determines whether a point is located inside a polygon.
  //            ILOC is set to +1 if outside, 0 if inside and -1 if the
  //            arguments are not valid.
  //   (Last changed on 18/ 3/01.)
  //-----------------------------------------------------------------------

  // Special case: x = y = 0
  if (x0 == 0. && y0 == 0.) return true;

  // Special case: round tube.
  if (n == 0) {
    if (x0 * x0 + y0 * y0 > a * a) return false;
    return true;
  }

  if (n < 0 || n == 1 || n == 2) {
    std::cerr << m_className << "::InTube:\n";
    std::cerr << "    Invalid number of edges (n = " << n << ")\n";
    return false;
  }

  // Truly polygonal tubes.
  // Reduce angle to the first sector.
  double phi = atan2(y0, x0);
  if (phi < 0.) phi += TwoPi;
  phi -= TwoPi * int(0.5 * n * phi / Pi) / n;
  // Compare the length to the local radius.
  if ((x0 * x0 + y0 * y0) * pow(cos(Pi / n - phi), 2) >
      a * a * pow(cos(Pi / n), 2))
    return false;

  return true;
}

void ComponentAnalyticField::InitializeCoefficientTables() {

  const int nterms = 16;

  // Tables of coefficients used by ConformalMap
  m_cc1.assign(6, std::vector<double>(nterms, 0.));
  m_cc2.assign(6, std::vector<double>(nterms, 0.));

  // Triangle: coefficients for centre and corner expansion.
  const double cc13[nterms] = {
      0.1000000000e+01, -.1666666865e+00, 0.3174602985e-01, -.5731921643e-02,
      0.1040112227e-02, -.1886279933e-03, 0.3421107249e-04, -.6204730198e-05,
      0.1125329618e-05, -.2040969207e-06, 0.3701631357e-07, -.6713513301e-08,
      0.1217605794e-08, -.2208327132e-09, 0.4005162868e-10, -.7264017512e-11};
  const double cc23[nterms] = {
      0.3333333135e+00, -.5555555597e-01, 0.1014109328e-01, -.1837154618e-02,
      0.3332451452e-03, -.6043842586e-04, 0.1096152027e-04, -.1988050826e-05,
      0.3605655365e-06, -.6539443120e-07, 0.1186035448e-07, -.2151069323e-08,
      0.3901317047e-09, -.7075676156e-10, 0.1283289534e-10, -.2327455936e-11};

  // Square: coefficients for centre and corner expansion.
  const double cc14[nterms] = {
      0.1000000000e+01, -.1000000238e+00, 0.8333332837e-02, -.7051283028e-03,
      0.5967194738e-04, -.5049648280e-05, 0.4273189802e-06, -.3616123934e-07,
      0.3060091514e-08, -.2589557457e-09, 0.2191374859e-10, -.1854418528e-11,
      0.1569274224e-12, -.1327975205e-13, 0.1123779363e-14, -.9509817570e-16};
  const double cc24[nterms] = {
      0.1000000000e+01, -.5000000000e+00, 0.3000000119e+00, -.1750000119e+00,
      0.1016666889e+00, -.5916666612e-01, 0.3442307562e-01, -.2002724260e-01,
      0.1165192947e-01, -.6779119372e-02, 0.3944106400e-02, -.2294691978e-02,
      0.1335057430e-02, -.7767395582e-03, 0.4519091453e-03, -.2629216760e-03};

  // Pentagon: coefficients for centre and corner expansion.
  const double cc15[nterms] = {
      0.1000000000e+01, -.6666666269e-01, 0.1212121220e-02, -.2626262140e-03,
      -.3322110570e-04, -.9413293810e-05, -.2570029210e-05, -.7695705904e-06,
      -.2422486887e-06, -.7945993730e-07, -.2691839640e-07, -.9361642128e-08,
      -.3327319087e-08, -.1204430555e-08, -.4428404310e-09, -.1650302672e-09};
  const double cc25[nterms] = {
      0.1248050690e+01, -.7788147926e+00, 0.6355384588e+00, -.4899077415e+00,
      0.3713272810e+00, -.2838423252e+00, 0.2174729109e+00, -.1663445234e+00,
      0.1271933913e+00, -.9728997946e-01, 0.7442557812e-01, -.5692918226e-01,
      0.4354400188e-01, -.3330700099e-01, 0.2547712997e-01, -.1948769018e-01};

  // Hexagon: coefficients for centre and corner expansion.
  const double cc16[nterms] = {
      0.1000000000e+01, -.4761904851e-01, -.1221001148e-02, -.3753788769e-03,
      -.9415557724e-04, -.2862767724e-04, -.9587882232e-05, -.3441659828e-05,
      -.1299798896e-05, -.5103651119e-06, -.2066504408e-06, -.8578405186e-07,
      -.3635090096e-07, -.1567239494e-07, -.6857355572e-08, -.3038770346e-08};
  const double cc26[nterms] = {
      0.1333333015e+01, -.8888888955e+00, 0.8395061493e+00, -.7242798209e+00,
      0.6016069055e+00, -.5107235312e+00, 0.4393203855e+00, -.3745460510e+00,
      0.3175755739e+00, -.2703750730e+00, 0.2308617830e+00, -.1966916919e+00,
      0.1672732830e+00, -.1424439549e+00, 0.1214511395e+00, -.1034612656e+00};

  // Heptagon: coefficients for centre and corner expansion.
  const double cc17[nterms] = {
      0.1000000000e+01, -.3571428731e-01, -.2040816238e-02, -.4936389159e-03,
      -.1446709794e-03, -.4963850370e-04, -.1877940667e-04, -.7600909157e-05,
      -.3232265954e-05, -.1427365532e-05, -.6493634714e-06, -.3026190711e-06,
      -.1438593245e-06, -.6953911225e-07, -.3409525462e-07, -.1692310647e-07};
  const double cc27[nterms] = {
      0.1359752655e+01, -.9244638681e+00, 0.9593217969e+00, -.8771237731e+00,
      0.7490229011e+00, -.6677658558e+00, 0.6196745634e+00, -.5591596961e+00,
      0.4905325770e+00, -.4393517375e+00, 0.4029803872e+00, -.3631100059e+00,
      0.3199430704e+00, -.2866140604e+00, 0.2627358437e+00, -.2368256450e+00};

  // Octagon: coefficients for centre and corner expansion.
  const double cc18[nterms] = {
      0.1000000000e+01, -.2777777612e-01, -.2246732125e-02, -.5571441725e-03,
      -.1790652314e-03, -.6708275760e-04, -.2766949183e-04, -.1219387286e-04,
      -.5640039490e-05, -.2706697160e-05, -.1337270078e-05, -.6763995657e-06,
      -.3488264610e-06, -.1828456675e-06, -.9718036154e-07, -.5227070332e-07};
  const double cc28[nterms] = {
      0.1362840652e+01, -.9286670089e+00, 0.1035511017e+01, -.9800255299e+00,
      0.8315343261e+00, -.7592730522e+00, 0.7612683773e+00, -.7132136226e+00,
      0.6074471474e+00, -.5554352999e+00, 0.5699443221e+00, -.5357525349e+00,
      0.4329345822e+00, -.3916820884e+00, 0.4401986003e+00, -.4197303057e+00};

  for (int i = 0; i < nterms; ++i) {
    m_cc1[0][i] = cc13[i];
    m_cc2[0][i] = cc23[i];
    m_cc1[1][i] = cc14[i];
    m_cc2[1][i] = cc24[i];
    m_cc1[2][i] = cc15[i];
    m_cc2[2][i] = cc25[i];
    m_cc1[3][i] = cc16[i];
    m_cc2[3][i] = cc26[i];
    m_cc1[4][i] = cc17[i];
    m_cc2[4][i] = cc27[i];
    m_cc1[5][i] = cc18[i];
    m_cc2[5][i] = cc28[i];
  }
}

void ComponentAnalyticField::Field3dA00(const double xpos, const double ypos,
                                        const double zpos, double& ex,
                                        double& ey, double& ez, double& volt) {

  //-----------------------------------------------------------------------
  //   E3DA00 - Subroutine adding 3-dimensional charges for A cells.
  //            The potential used is 1/2*pi*eps0  1/r
  //   VARIABLES : EX, EY     : x,y-component of the electric field.
  //               ETOT       : Magnitude of electric field.
  //               VOLT       : Potential.
  //               EXHELP etc : One term in the series to be summed.
  //               (XPOS,YPOS): The position where the field is calculated.
  //   (Last changed on  5/12/94.)
  //-----------------------------------------------------------------------

  double exhelp, eyhelp, ezhelp, vhelp;

  // Initialise the electric field and potential.
  ex = ey = ez = volt = 0.;

  // Loop over all charges.
  const unsigned int n3d = m_ch3d.size();
  for (unsigned int i = 0; i < n3d; ++i) {
    // Calculate the field in case there are no planes.
    const double dx = xpos - m_ch3d[i].x;
    const double dy = ypos - m_ch3d[i].y;
    const double dz = zpos - m_ch3d[i].z;
    const double r = sqrt(dx * dx + dy * dy + dz * dz);
    if (fabs(r) < Small) continue;
    const double r3 = pow(r, 3);
    exhelp = -dx / r3;
    eyhelp = -dy / r3;
    ezhelp = -dz / r3;
    vhelp = 1. / r;
    // Take care of a plane at constant x.
    double dxm = 0., dym = 0.;
    if (m_ynplax) {
      dxm = m_ch3d[i].x + xpos - 2 * m_coplax;
      const double rplan = sqrt(dxm * dxm + dy * dy);
      if (fabs(rplan) < Small) continue;
      const double rplan3 = pow(rplan, 3);
      exhelp += dxm / rplan3;
      eyhelp += dy / rplan3;
      ezhelp += dz / rplan3;
      vhelp -= 1. / rplan;
    }
    // Take care of a plane at constant y.
    if (m_ynplay) {
      dym = m_ch3d[i].y + ypos - 2. * m_coplay;
      const double rplan = sqrt(dx * dx + dym * dym);
      if (fabs(rplan) < Small) continue;
      const double rplan3 = pow(rplan, 3);
      exhelp += dx / rplan3;
      eyhelp += dym / rplan3;
      ezhelp += dz / rplan3;
      vhelp -= 1. / rplan;
    }
    // Take care of pairs of planes.
    if (m_ynplax && m_ynplay) {
      const double rplan = sqrt(dxm * dxm + dym * dym);
      if (fabs(rplan) < Small) continue;
      const double rplan3 = pow(rplan, 3);
      exhelp -= dxm / rplan3;
      eyhelp -= dym / rplan3;
      ezhelp -= dz / rplan3;
      vhelp += 1. / rplan;
    }
    // Add the terms to the electric field and the potential.
    ex -= m_ch3d[i].e * exhelp;
    ey -= m_ch3d[i].e * eyhelp;
    ez -= m_ch3d[i].e * ezhelp;
    volt += m_ch3d[i].e * vhelp;
  }
}

void ComponentAnalyticField::Field3dB2X(const double xpos, const double ypos,
                                        const double zpos, double& ex,
                                        double& ey, double& ez, double& volt) {

  //-----------------------------------------------------------------------
  //   E3DB2X - Routine calculating the potential for a 3 dimensional point
  //            charge between two plates at constant x.
  //            The series expansions for the modified Bessel functions
  //            have been taken from Abramowitz and Stegun.
  //   VARIABLES : See routine E3DA00 for most of the variables.
  //   (Last changed on  5/12/94.)
  //-----------------------------------------------------------------------

  const double rcut = 1.;

  double rr1, rr2, rm1, rm2, err, ezz;
  double exsum = 0., eysum = 0., ezsum = 0., vsum = 0.;
  double k0r, k1r, k0rm, k1rm;

  // Initialise the sums for the field components.
  ex = ey = ez = volt = 0.;

  // Loop over all charges.
  const unsigned int n3d = m_ch3d.size();
  for (unsigned int i = 0; i < n3d; ++i) {
    // Skip wires that are on the charge.
    if (xpos == m_ch3d[i].x && ypos == m_ch3d[i].y && zpos == m_ch3d[i].z)
      continue;
    const double dx = xpos - m_ch3d[i].x;
    const double dy = ypos - m_ch3d[i].y;
    const double dz = zpos - m_ch3d[i].z;
    const double dxm = xpos + m_ch3d[i].x - 2 * m_coplax;
    // In the far away zone, sum the modified Bessel function series.
    if (dy * dy + dz * dz > pow(rcut * 2 * m_sx, 2)) {
      // Initialise the per-wire sum.
      exsum = eysum = ezsum = vsum = 0.;
      // Loop over the terms in the series.
      for (unsigned int j = 1; j <= m_nTermBessel; ++j) {
        // Obtain reduced coordinates.
        const double rr = Pi * j * sqrt(dy * dy + dz * dz) / m_sx;
        const double zzp = Pi * j * dx / m_sx;
        const double zzn = Pi * j * dxm / m_sx;
        // Evaluate the Bessel functions for this R.
        if (rr < 2.) {
          k0r = Numerics::BesselK0S(rr);
          k1r = Numerics::BesselK1S(rr);
        } else {
          k0r = Numerics::BesselK0L(rr);
          k1r = Numerics::BesselK1L(rr);
        }
        // Get the field components.
        const double czzp = cos(zzp);
        const double czzn = cos(zzn);
        vsum += (1. / m_sx) * k0r * (czzp - czzn);
        err = (TwoPi * j / (m_sx * m_sx)) * k1r * (czzp - czzn);
        ezz = (TwoPi * j / (m_sx * m_sx)) * k0r * (sin(zzp) - sin(zzn));
        exsum += ezz;
        eysum += err * dy / sqrt(dy * dy + dz * dz);
        ezsum += err * dz / sqrt(dy * dy + dz * dz);
        continue;
      }
    } else {
      // Direct polynomial summing, obtain reduced coordinates.
      // Loop over the terms.
      for (unsigned int j = 0; j <= m_nTermPoly; ++j) {
        // Simplify the references to the distances.
        rr1 = sqrt(pow(dx + j * 2 * m_sx, 2) + dy * dy + dz * dz);
        rr2 = sqrt(pow(dx - j * 2 * m_sx, 2) + dy * dy + dz * dz);
        rm1 = sqrt(pow(dxm - j * 2 * m_sx, 2) + dy * dy + dz * dz);
        rm2 = sqrt(pow(dxm + j * 2 * m_sx, 2) + dy * dy + dz * dz);
        const double rr13 = pow(rr1, 3);
        const double rm13 = pow(rm1, 3);
        // Initialisation of the sum: only a charge and a mirror charge.
        if (j == 0) {
          vsum = 1. / rr1 - 1. / rm1;
          exsum = dx / rr13 - dxm / rm13;
          eysum = dy * (1. / rr13 - 1. / rm13);
          ezsum = dz * (1. / rr13 - 1. / rm13);
          continue;
        }
        const double rr23 = pow(rr2, 3);
        const double rm23 = pow(rm2, 3);
        // Further terms in the series: 2 charges and 2 mirror charges.
        vsum += 1. / rr1 + 1. / rr2 - 1. / rm1 - 1. / rm2;
        exsum += (dx + j * 2 * m_sx) / rr13 + (dx - j * 2 * m_sx) / rr23 -
                 (dxm - j * 2 * m_sx) / rm13 - (dxm + j * 2 * m_sx) / rm23;
        eysum += dy * (1. / rr13 + 1. / rr23 - 1. / rm13 - 1. / rm23);
        ezsum += dz * (1. / rr13 + 1. / rr23 - 1. / rm13 - 1. / rm23);
      }
    }
    // Take care of a plane at constant y.
    if (m_ynplay) {
      const double dym = ypos + m_ch3d[i].y - 2. * m_coplay;
      if (dym * dym + dz * dz > pow(rcut * 2 * m_sx, 2)) {
        // Bessel function series.
        // Loop over the terms in the series.
        for (unsigned int j = 1; j <= m_nTermBessel; ++j) {
          // Obtain reduced coordinates.
          const double rrm = Pi * j * sqrt(dym * dym + dz * dz) / m_sx;
          const double zzp = Pi * j * dx / m_sx;
          const double zzn = Pi * j * dxm / m_sx;
          // Evaluate the Bessel functions for this R.
          if (rrm < 2.) {
            k0rm = Numerics::BesselK0S(rrm);
            k1rm = Numerics::BesselK1S(rrm);
          } else {
            k0rm = Numerics::BesselK0L(rrm);
            k1rm = Numerics::BesselK1L(rrm);
          }
          // Get the field components.
          const double czzp = cos(zzp);
          const double czzn = cos(zzn);
          vsum += (1. / m_sx) * k0rm * (czzp - czzn);
          err = (TwoPi / (m_sx * m_sx)) * k1rm * (czzp - czzn);
          ezz = (TwoPi / (m_sx * m_sx)) * k0rm * (sin(zzp) - sin(zzn));
          exsum += ezz;
          eysum += err * dym / sqrt(dym * dym + dz * dz);
          ezsum += err * dz / sqrt(dym * dym + dz * dz);
        }
      } else {
        // Polynomial sum.
        // Loop over the terms.
        for (unsigned int j = 0; j <= m_nTermPoly; ++j) {
          // Simplify the references to the distances.
          rr1 = sqrt(pow(dx + j * 2 * m_sx, 2) + dym * dym + dz * dz);
          rr2 = sqrt(pow(dx - j * 2 * m_sx, 2) + dym * dym + dz * dz);
          rm1 = sqrt(pow(dxm - j * 2 * m_sx, 2) + dym * dym + dz * dz);
          rm2 = sqrt(pow(dxm + j * 2 * m_sx, 2) + dym * dym + dz * dz);
          const double rr13 = pow(rr1, 3);
          const double rm13 = pow(rm1, 3);
          // Initialisation of the sum: only a charge and a mirror charge.
          if (j == 0) {
            vsum += -1. / rr1 + 1. / rm1;
            exsum += -dx / rr13 + dxm / rm13;
            eysum += -dym * (1. / rr13 - 1. / rm13);
            ezsum += -dz * (1. / rr13 - 1. / rm13);
            continue;
          }
          const double rr23 = pow(rr2, 3);
          const double rm23 = pow(rm2, 3);
          // Further terms in the series: 2 charges and 2 mirror charges.
          vsum += -1. / rr1 - 1. / rr2 + 1. / rm1 + 1. / rm2;
          exsum += -(dx + j * 2 * m_sx) / rr13 - (dx - j * 2 * m_sx) / rr23 +
                   (dxm - j * 2 * m_sx) / rm13 + (dxm + j * 2 * m_sx) / rm23;
          eysum += -dym * (1. / rr13 + 1. / rr23 - 1. / rm13 - 1. / rm23);
          ezsum += -dz * (1. / rr13 + 1. / rr23 - 1. / rm13 - 1. / rm23);
        }
      }
    }
    ex += m_ch3d[i].e * exsum;
    ey += m_ch3d[i].e * eysum;
    ez += m_ch3d[i].e * ezsum;
    volt += m_ch3d[i].e * vsum;
  }
}

void ComponentAnalyticField::Field3dB2Y(const double xpos, const double ypos,
                                        const double zpos, double& ex,
                                        double& ey, double& ez, double& volt) {

  //-----------------------------------------------------------------------
  //   E3DB2Y - Routine calculating the potential for a 3 dimensional point
  //            charge between two plates at constant y.
  //            The series expansions for the modified Bessel functions
  //            have been taken from Abramowitz and Stegun.
  //   VARIABLES : See routine E3DA00 for most of the variables.
  //   (Last changed on  5/12/94.)
  //-----------------------------------------------------------------------

  const double rcut = 1.;

  double rr1, rr2, rm1, rm2, err, ezz;
  double exsum = 0., eysum = 0., ezsum = 0., vsum = 0.;
  double k0r, k1r, k0rm, k1rm;

  // Initialise the sums for the field components.
  ex = ey = ez = volt = 0.;

  // Loop over all charges.
  const unsigned int n3d = m_ch3d.size();
  for (unsigned int i = 0; i < n3d; ++i) {
    // Skip wires that are on the charge.
    if (xpos == m_ch3d[i].x && ypos == m_ch3d[i].y && zpos == m_ch3d[i].z)
      continue;
    const double dx = xpos - m_ch3d[i].x;
    const double dy = ypos - m_ch3d[i].y;
    const double dz = zpos - m_ch3d[i].z;
    const double dym = ypos + m_ch3d[i].y - 2 * m_coplay;
    // In the far away zone, sum the modified Bessel function series.
    if (dx * dx + dz * dz > pow(rcut * 2 * m_sy, 2)) {
      // Initialise the per-wire sum.
      exsum = eysum = ezsum = vsum = 0.;
      // Loop over the terms in the series.
      for (unsigned int j = 1; j <= m_nTermBessel; ++j) {
        // Obtain reduced coordinates.
        const double rr = Pi * j * sqrt(dx * dx + dz * dz) / m_sy;
        const double zzp = Pi * j * dy / m_sy;
        const double zzn = Pi * j * dym / m_sy;
        // Evaluate the Bessel functions for this R.
        if (rr < 2.) {
          k0r = Numerics::BesselK0S(rr);
          k1r = Numerics::BesselK1S(rr);
        } else {
          k0r = Numerics::BesselK0L(rr);
          k1r = Numerics::BesselK1L(rr);
        }
        // Get the field components.
        const double czzp = cos(zzp);
        const double czzn = cos(zzn);
        vsum += (1. / m_sy) * k0r * (czzp - czzn);
        err = (TwoPi * j / (m_sy * m_sy)) * k1r * (czzp - czzn);
        ezz = (TwoPi * j / (m_sy * m_sy)) * k0r * (sin(zzp) - sin(zzn));
        exsum += err * dx / sqrt(dx * dx + dz * dz);
        ezsum += err * dz / sqrt(dx * dx + dz * dz);
        eysum += ezz;
        continue;
      }
    } else {
      // Direct polynomial summing, obtain reduced coordinates.
      // Loop over the terms.
      for (unsigned int j = 0; j <= m_nTermPoly; ++j) {
        // Simplify the references to the distances.
        rr1 = sqrt(dx * dx + dz * dz + pow(dy + j * 2 * m_sy, 2));
        rr2 = sqrt(dx * dx + dz * dz + pow(dy - j * 2 * m_sy, 2));
        rm1 = sqrt(dx * dx + dz * dz + pow(dym - j * 2 * m_sy, 2));
        rm2 = sqrt(dx * dx + dz * dz + pow(dym + j * 2 * m_sy, 2));
        const double rr13 = pow(rr1, 3);
        const double rm13 = pow(rm1, 3);
        // Initialisation of the sum: only a charge and a mirror charge.
        if (j == 0) {
          vsum = 1. / rr1 - 1. / rm1;
          exsum = dx * (1. / rr13 - 1. / rm13);
          ezsum = dz * (1. / rr13 - 1. / rm13);
          eysum = dy / rr13 - dym / rm13;
          continue;
        }
        // Further terms in the series: 2 charges and 2 mirror charges.
        const double rr23 = pow(rr2, 3);
        const double rm23 = pow(rm2, 3);
        vsum += 1. / rr1 + 1. / rr2 - 1. / rm1 - 1. / rm2;
        exsum += dx * (1. / rr13 + 1. / rr23 - 1. / rm13 - 1. / rm23);
        ezsum += dz * (1. / rr13 + 1. / rr23 - 1. / rm13 - 1. / rm23);
        eysum += (dy + j * 2 * m_sy) / rr13 + (dy - j * 2 * m_sy) / rr23 -
                 (dym - j * 2 * m_sy) / rm13 - (dym + j * 2 * m_sy) / rm23;
      }
    }
    // Take care of a plane at constant x.
    if (m_ynplax) {
      const double dxm = xpos + m_ch3d[i].x - 2. * m_coplax;
      if (dxm * dxm + dz * dz > pow(rcut * 2 * m_sy, 2)) {
        // Bessel function series.
        // Loop over the terms in the series.
        for (unsigned int j = 1; j <= m_nTermBessel; ++j) {
          // Obtain reduced coordinates.
          const double rrm = Pi * j * sqrt(dxm * dxm + dz * dz) / m_sy;
          const double zzp = Pi * j * dy / m_sy;
          const double zzn = Pi * j * dym / m_sy;
          // Evaluate the Bessel functions for this R.
          if (rrm < 2.) {
            k0rm = Numerics::BesselK0S(rrm);
            k1rm = Numerics::BesselK1S(rrm);
          } else {
            k0rm = Numerics::BesselK0L(rrm);
            k1rm = Numerics::BesselK1L(rrm);
          }
          // Get the field components.
          const double czzp = cos(zzp);
          const double czzn = cos(zzn);
          vsum += (1. / m_sy) * k0rm * (czzp - czzn);
          err = (TwoPi / (m_sy * m_sy)) * k1rm * (czzp - czzn);
          ezz = (TwoPi / (m_sy * m_sy)) * k0rm * (sin(zzp) - sin(zzn));
          exsum += err * dxm / sqrt(dxm * dxm + dz * dz);
          ezsum += err * dz / sqrt(dxm * dxm + dz * dz);
          eysum += ezz;
        }
      } else {
        // Polynomial sum.
        // Loop over the terms.
        for (unsigned int j = 0; j <= m_nTermPoly; ++j) {
          // Simplify the references to the distances.
          rr1 = sqrt(pow(dy + j * 2 * m_sy, 2) + dxm * dxm + dz * dz);
          rr2 = sqrt(pow(dy - j * 2 * m_sy, 2) + dxm * dxm + dz * dz);
          rm1 = sqrt(pow(dym - j * 2 * m_sy, 2) + dxm * dxm + dz * dz);
          rm2 = sqrt(pow(dym + j * 2 * m_sy, 2) + dxm * dxm + dz * dz);
          const double rr13 = pow(rr1, 3);
          const double rm13 = pow(rm1, 3);
          // Initialisation of the sum: only a charge and a mirror charge.
          if (j == 0) {
            vsum += -1. / rr1 + 1. / rm1;
            exsum += -dxm * (1. / rr13 - 1. / rm13);
            ezsum += -dz * (1. / rr13 - 1. / rm13);
            eysum += -dy / rr13 + dym / rm13;
            continue;
          }
          const double rr23 = pow(rr2, 3);
          const double rm23 = pow(rm2, 3);
          // Further terms in the series: 2 charges and 2 mirror charges.
          vsum += -1. / rr1 - 1. / rr2 + 1. / rm1 + 1. / rm2;
          exsum += -dxm * (1. / rr13 + 1. / rr23 - 1. / rm13 - 1. / rm23);
          ezsum += -dz * (1. / rr13 + 1. / rr23 - 1. / rm13 - 1. / rm23);
          eysum += -(dy + j * 2 * m_sy) / rr13 - (dy - j * 2 * m_sy) / rr23 +
                   (dym - j * 2 * m_sy) / rm13 + (dym + j * 2 * m_sy) / rm23;
        }
      }
    }
    ex += m_ch3d[i].e * exsum;
    ey += m_ch3d[i].e * eysum;
    ez += m_ch3d[i].e * ezsum;
    volt += m_ch3d[i].e * vsum;
  }
}

void ComponentAnalyticField::Field3dD10(const double xxpos, const double yypos,
                                        const double zzpos, double& eex,
                                        double& eey, double& eez,
                                        double& volt) {

  //-----------------------------------------------------------------------
  //   E3DD10 - Subroutine adding 3-dimensional charges to tubes with one
  //            wire running down the centre.
  //            The series expansions for the modified Bessel functions
  //            have been taken from Abramowitz and Stegun.
  //   VARIABLES : See routine E3DA00 for most of the variables.
  //   (Last changed on 25/11/95.)
  //-----------------------------------------------------------------------

  const double rcut = 1.;

  double x3d, y3d, z3d;
  double exsum = 0., eysum = 0., ezsum = 0., vsum = 0.;
  double rr1, rr2, rm1, rm2, err, ezz;
  double k0r, k1r;

  // Initialise the sums for the field components.
  eex = eey = eez = volt = 0.;
  double ex = 0., ey = 0., ez = 0.;

  // Ensure that the routine can actually work.
  if (m_nWires < 1) {
    std::cerr << m_className << "::Field3dD10:\n";
    std::cerr << "    Inappropriate potential function.\n";
    return;
  }

  // Define a periodicity and one plane in the mapped frame.
  const double ssx = log(2. * m_cotube / m_w[0].d);
  const double cpl = log(m_w[0].d / 2.);

  // Transform the coordinates to the mapped frame.
  const double xpos = 0.5 * log(xxpos * xxpos + yypos * yypos);
  const double ypos = atan2(yypos, xxpos);
  const double zpos = zzpos;

  // Loop over all point charges.
  const unsigned int n3d = m_ch3d.size();
  for (unsigned int i = 0; i < n3d; ++i) {
    for (int ii = -1; ii <= 1; ++ii) {
      x3d = 0.5 * log(m_ch3d[i].x * m_ch3d[i].x + m_ch3d[i].y * m_ch3d[i].y);
      y3d = atan2(m_ch3d[i].y, m_ch3d[i].x + ii * TwoPi);
      z3d = m_ch3d[i].z;
      const double dx = xpos - x3d;
      const double dy = ypos - y3d;
      const double dz = zpos - z3d;
      const double dxm = xpos + x3d - 2 * cpl;
      // Skip wires that are on the charge.
      if (xpos == x3d && ypos == y3d && zpos == z3d) continue;
      // In the far away zone, sum the modified Bessel function series.
      if (dy * dy + dz * dz > pow(rcut * 2 * ssx, 2)) {
        // Initialise the per-wire sum.
        exsum = eysum = ezsum = vsum = 0.;
        // Loop over the terms in the series.
        for (unsigned int j = 1; j <= m_nTermBessel; ++j) {
          // Obtain reduced coordinates.
          const double rr = Pi * j * sqrt(dy * dy + dz * dz) / ssx;
          const double zzp = Pi * j * dx / ssx;
          const double zzn = Pi * j * dxm / ssx;
          // Evaluate the Bessel functions for this R.
          if (rr < 2.) {
            k0r = Numerics::BesselK0S(rr);
            k1r = Numerics::BesselK1S(rr);
          } else {
            k0r = Numerics::BesselK0L(rr);
            k1r = Numerics::BesselK1L(rr);
          }
          // Get the field components.
          const double czzp = cos(zzp);
          const double czzn = cos(zzn);
          vsum += (1. / ssx) * k0r * (czzp - czzn);
          err = (j * TwoPi / (ssx * ssx)) * k1r * (czzp - czzn);
          ezz = (j * TwoPi / (ssx * ssx)) * k0r * (sin(zzp) - sin(zzn));
          exsum += ezz;
          eysum += err * dy / sqrt(dy * dy + dz * dz);
          ezsum += err * dz / sqrt(dy * dy + dz * dz);
        }
      } else {
        // Direct polynomial summing, obtain reduced coordinates.
        // Loop over the terms.
        // TODO: <=?
        for (unsigned int j = 0; j < m_nTermPoly; ++j) {
          // Simplify the references to the distances.
          rr1 = sqrt(pow(dx + j * 2 * ssx, 2) + dy * dy + dz * dz);
          rr2 = sqrt(pow(dx - j * 2 * ssx, 2) + dy * dy + dz * dz);
          rm1 = sqrt(pow(dxm - j * 2 * ssx, 2) + dy * dy + dz * dz);
          rm2 = sqrt(pow(dxm + j * 2 * ssx, 2) + dy * dy + dz * dz);
          const double rr13 = pow(rr1, 3);
          const double rm13 = pow(rm1, 3);
          // Initialisation of the sum: only a charge and a mirror charge.
          if (j == 0) {
            vsum = 1. / rr1 - 1. / rm1;
            exsum = dxm / rr13 - dxm / rm13;
            eysum = dy * (1. / rr13 - 1. / rm13);
            ezsum = dz * (1. / rr13 - 1. / rm13);
            continue;
          }
          const double rr23 = pow(rr2, 3);
          const double rm23 = pow(rm2, 3);
          // Further terms in the series: 2 charges and 2 mirror charges.
          vsum += 1. / rr1 + 1. / rr2 - 1. / rm1 - 1. / rm2;
          exsum += (dx + j * 2 * ssx) / rr13 + (dx - j * 2 * ssx) / rr23 -
                   (dxm - j * 2 * ssx) / rm13 - (dxm + j * 2 * ssx) / rm23;
          eysum += dy * (1. / rr13 + 1. / rr23 - 1. / rm13 - 1. / rm23);
          ezsum += dz * (1. / rr13 + 1. / rr23 - 1. / rm13 - 1. / rm23);
        }
      }
      ex += m_ch3d[i].e * exsum;
      ey += m_ch3d[i].e * eysum;
      ez += m_ch3d[i].e * ezsum;
      // Finish the loop over the charges.
    }
  }

  // Transform the field vectors back to Cartesian coordinates.
  eex = exp(-xpos) * (ex * cos(ypos) - ey * sin(ypos));
  eey = exp(-ypos) * (ex * sin(ypos) + ey * cos(ypos));
  eez = ez;
}

bool ComponentAnalyticField::PrepareSignals() {

  //-----------------------------------------------------------------------
  //   SIGINI - Initialises signal calculations.
  //   (Last changed on 11/10/06.)
  //-----------------------------------------------------------------------

  if (m_readout.empty()) {
    std::cerr << m_className << "::PrepareSignals:\n";
    std::cerr << "    There are no readout groups defined.\n";
    std::cerr << "    Calculation of weighting fields makes no sense.\n";
    return false;
  }

  if (!m_cellset) {
    if (!Prepare()) {
      std::cerr << m_className << "::PrepareSignals:\n";
      std::cerr << "    Cell could not be set up.\n";
      std::cerr << "    No calculation of weighting fields possible.\n";
      return false;
    }
  }

  // If using natural periodicity, copy the cell type.
  // Otherwise, eliminate true periodicities.
  if (nFourier == 0) {
    m_scellTypeFourier = m_scellType;
  } else if (m_scellType == "A  " || m_scellType == "B1X" ||
             m_scellType == "B1Y" || m_scellType == "C1 ") {
    m_scellTypeFourier = "A  ";
  } else if (m_scellType == "B2X" || m_scellType == "C2X") {
    m_scellTypeFourier = "B2X";
  } else if (m_scellType == "B2Y" || m_scellType == "C2Y") {
    m_scellTypeFourier = "B2Y";
  } else if (m_scellType == "C3 ") {
    m_scellTypeFourier = "C3 ";
  } else if (m_scellType == "D1 ") {
    m_scellTypeFourier = "D1 ";
  } else if (m_scellType == "D3 ") {
    m_scellTypeFourier = "D3 ";
  } else {
    // Other cases.
    std::cerr << m_className << "::PrepareSignals:\n";
    std::cerr << "    No potentials available to handle cell type "
              << m_scellType << ".\n";
    return false;
  }

  // Establish the directions in which convolutions occur.
  fperx = fpery = false;
  if (nFourier == 0) {
    mfexp = 0;
  } else {
    if (m_scellType == "B1X" || m_scellType == "C1 " || m_scellType == "C2Y") {
      fperx = true;
    }
    if (m_scellType == "B1Y" || m_scellType == "C1 " || m_scellType == "C2X") {
      fpery = true;
    }
    mfexp = int(0.1 + log(1. * nFourier) / log(2.));
    if (mfexp == 0) {
      fperx = fpery = false;
    }
  }
  // Set maximum and minimum Fourier terms.
  mxmin = mymin = mxmax = mymax = 0;
  if (fperx) {
    mxmin = std::min(0, 1 - nFourier / 2);
    mxmax = nFourier / 2;
  }
  if (fpery) {
    mymin = std::min(0, 1 - nFourier / 2);
    mymax = nFourier / 2;
  }

  // Print some debugging output if requested.
  if (m_debug) {
    std::cout << m_className << "::PrepareSignals:\n";
    std::cout << "    Cell type:           " << m_scellType << "\n";
    std::cout << "    Fourier cell type:   " << m_scellTypeFourier << "\n";
    std::cout << "    x convolutions:      " << fperx << "\n";
    std::cout << "    y convolutions:      " << fpery << "\n";
    std::cout << "    No of Fourier terms: " << nFourier << " (= 2**" << mfexp
              << ")\n";
  }

  // Prepare the signal matrices.
  if (!SetupWireSignals()) {
    std::cerr << m_className << "::PrepareSignals:\n";
    std::cerr << "    Preparing wire signal capacitance matrices failed.\n";
    m_sigmat.clear();
    return false;
  }
  if (!SetupPlaneSignals()) {
    std::cerr << m_className << "::PrepareSignals:\n";
    std::cerr << "    Preparing plane charges failed.\n";
    m_sigmat.clear();
    m_qplane.clear();
    return false;
  }

  // And open the signal file.
  // CALL SIGIST('OPEN',0,DUMMY,DUMMY,0,0,0,0,IFAIL1)

  // Associate wires, planes and strips with readout groups
  const unsigned int nReadout = m_readout.size();
  for (unsigned int i = 0; i < nReadout; ++i) {
    for (unsigned int j = 0; j < m_nWires; ++j) {
      if (m_w[j].type == m_readout[i]) m_w[j].ind = i;
    }
    for (unsigned int j = 0; j < 5; ++j) {
      if (planes[j].type == m_readout[i]) planes[j].ind = i;
      const unsigned int nStrips1 = planes[j].strips1.size();
      for (unsigned int k = 0; k < nStrips1; ++k) {
        if (planes[j].strips1[k].type == m_readout[i]) {
          planes[j].strips1[k].ind = i;
        }
      }
      const unsigned int nStrips2 = planes[j].strips2.size();
      for (unsigned int k = 0; k < nStrips2; ++k) {
        if (planes[j].strips2[k].type == m_readout[i]) {
          planes[j].strips2[k].ind = i;
        }
      }
      const unsigned int nPixels = planes[j].pixels.size();
      for (unsigned int k = 0; k < nPixels; ++k) {
        if (planes[j].pixels[k].type == m_readout[i]) {
          planes[j].pixels[k].ind = i;
        }
      }
    }
  }

  // Seems to have worked.
  m_sigset = true;
  return true;
}

bool ComponentAnalyticField::SetupWireSignals() {

  //-----------------------------------------------------------------------
  //   SIGIPR - Prepares the ion tail calculation by filling the signal
  //            matrices (ie non-periodic capacitance matrices),
  //            Fourier transforming them if necessary, inverting them and
  //            Fourier transforming them back. Because of the large number
  //            of terms involved, a (scratch) external file on unit 13 is
  //            used to store the intermediate and final results. This file
  //            is handled by the routines IONBGN and IONIO.
  //   VARIABLES : FFTMAT      : Matrix used for Fourier transforms.
  //   (Last changed on  4/10/06.)
  //-----------------------------------------------------------------------

  m_sigmat.resize(m_nWires);
  for (unsigned int i = 0; i < m_nWires; ++i) {
    m_sigmat[i].clear();
    m_sigmat[i].resize(m_nWires);
  }

  std::vector<std::vector<std::complex<double> > > fftmat;
  fftmat.clear();

  if (fperx || fpery) {
    fftmat.resize(nFourier);
    for (int i = 0; i < nFourier; ++i) {
      fftmat[i].resize(m_nWires);
    }
  }

  if (fperx || fpery) {
    // CALL IONBGN(IFAIL)
    // IF(IFAIL.EQ.1)THEN
    //     PRINT *,' !!!!!! SIGIPR WARNING : No storage'
    //             ' available for the signal matrices; no'
    //             ' induced currents.'
    //     RETURN
    // ENDIF
  }

  // Have the matrix/matrices filled (and stored).
  for (int mx = mxmin; mx <= mxmax; ++mx) {
    for (int my = mymin; my <= mymax; ++my) {
      // Select layer to be produced.
      if (m_scellTypeFourier == "A  ") {
        IprA00(mx, my);
      } else if (m_scellTypeFourier == "B2X") {
        IprB2X(my);
      } else if (m_scellTypeFourier == "B2Y") {
        IprB2Y(mx);
      } else if (m_scellTypeFourier == "C2X") {
        IprC2X();
      } else if (m_scellTypeFourier == "C2Y") {
        IprC2Y();
      } else if (m_scellTypeFourier == "C3 ") {
        IprC30();
      } else if (m_scellTypeFourier == "D1 ") {
        IprD10();
      } else if (m_scellTypeFourier == "D3 ") {
        IprD30();
      } else {
        std::cerr << m_className << "::SetupWireSignals:\n";
        std::cerr << "    Unknown signal cell type " << m_scellTypeFourier
                  << "\n";
        return false;
      }
      if (m_debug) {
        std::cout << m_className << "::SetupWireSignals:\n";
        std::cout << "    Signal matrix MX = " << mx << ", MY = " << my
                  << " has been calculated.\n";
      }
      if (fperx || fpery) {
        // Store the matrix.
        // CALL IONIO(MX,MY,1,0,IFAIL)
        // Quit if storing failed.
        // IF(IFAIL.NE.0)GOTO 2010
      }
      // Dump the signal matrix before inversion, if DEBUG is requested.
      if (m_debug) {
        std::cout << m_className << "::SetupWireSignals:\n";
        std::cout << "    Dump of signal matrix (" << mx << ", " << my
                  << ") before inversion:\n";
        for (unsigned int i = 0; i < m_nWires; i += 10) {
          for (unsigned int j = 0; j < m_nWires; j += 10) {
            std::cout << "    (Re-Block " << i / 10 << ", " << j / 10 << ")\n";
            for (unsigned int ii = 0; ii < 10; ++ii) {
              if (i + ii >= m_nWires) break;
              for (unsigned int jj = 0; jj < 10; ++jj) {
                if (j + jj >= m_nWires) break;
                std::cout << real(m_sigmat[i + ii][j + jj]) << "  ";
              }
              std::cout << "\n";
            }
            std::cout << "\n";
            std::cout << "    (Im-Block " << i / 10 << ", " << j / 10 << ")\n";
            for (unsigned int ii = 0; ii < 10; ++ii) {
              if (i + ii >= m_nWires) break;
              for (unsigned int jj = 0; jj < 10; ++jj) {
                if (j + jj >= m_nWires) break;
                std::cout << imag(m_sigmat[i + ii][j + jj]) << "  ";
              }
              std::cout << "\n";
            }
            std::cout << "\n";
          }
        }
        std::cout << m_className << "::SetupWireSignals:\n";
        std::cout << "    End of the uninverted capacitance matrix dump.\n";
      }
      // Next layer.
    }
  }

  // Have them fourier transformed (singly periodic case).
  if ((fperx && !fpery) || (fpery && !fperx)) {
    for (unsigned int i = 0; i < m_nWires; ++i) {
      for (int m = -nFourier / 2; m < nFourier / 2; ++m) {
        // CALL IONIO(M,M,2,I,IFAIL)
        //  IF(IFAIL.NE.0)GOTO 2010
        for (unsigned int j = 0; j < m_nWires; ++j) {
          fftmat[m + nFourier / 2][j] = m_sigmat[i][j];
        }
      }
      for (unsigned int j = 0; j < m_nWires; ++j) {
        // CALL CFFT(FFTMAT(1,J),MFEXP)
      }
      for (int m = -nFourier / 2; m < nFourier / 2; ++m) {
        // CALL IONIO(M,M,2,I,IFAIL)
        // IF(IFAIL.NE.0)GOTO 2010
        for (unsigned int j = 0; j < m_nWires; ++j) {
          m_sigmat[i][j] = fftmat[m + nFourier / 2][j];
        }
        // CALL IONIO(M,M,1,I,IFAIL)
        // IF(IFAIL.NE.0)GOTO 2010
      }
    }
  }
  // Have them fourier transformed (doubly periodic case).
  if (fperx || fpery) {
    for (unsigned int i = 0; i < m_nWires; ++i) {
      for (int mx = mxmin; mx <= mxmax; ++mx) {
        for (int my = mymin; my <= mymax; ++my) {
          // CALL IONIO(MX,MY,2,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
          for (unsigned int j = 0; j < m_nWires; ++j) {
            fftmat[my + nFourier / 2 - 1][j] = m_sigmat[i][j];
          }
        }
        for (unsigned int j = 0; j < m_nWires; ++j) {
          // CALL CFFT(FFTMAT(1,J),MFEXP)
        }
        for (int my = mymin; my <= mymax; ++my) {
          // CALL IONIO(MX,MY,2,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
          for (unsigned int j = 0; j < m_nWires; ++j) {
            m_sigmat[i][j] = fftmat[my + nFourier / 2 - 1][j];
          }
          // CALL IONIO(MX,MY,1,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
        }
      }
      for (int my = mymin; my <= mymax; ++my) {
        for (int mx = mxmin; mx <= mxmax; ++mx) {
          // CALL IONIO(MX,MY,2,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
          for (unsigned int j = 0; j < m_nWires; ++j) {
            fftmat[mx + nFourier / 2 - 1][j] = m_sigmat[i][j];
          }
        }
        for (unsigned int j = 0; j < m_nWires; ++j) {
          // CALL CFFT(FFTMAT(1,J),MFEXP)
        }
        for (int mx = mxmin; mx <= mxmax; ++mx) {
          // CALL IONIO(MX,MY,2,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
          for (unsigned int j = 0; j < m_nWires; ++j) {
            m_sigmat[i][j] = fftmat[mx + nFourier / 2 - 1][j];
          }
          // CALL IONIO(MX,MY,1,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
        }
      }
    }
  }

  // Invert the matrices.
  for (int mx = mxmin; mx <= mxmax; ++mx) {
    for (int my = mymin; my <= mymax; ++my) {
      // Retrieve the layer.
      if (fperx || fpery) {
        // CALL IONIO(MX,MY,2,0,IFAIL)
        // IF(IFAIL.NE.0)GOTO 2010
      }
      // Invert.
      if (m_nWires >= 1) {
        int ifail = 0;
        Numerics::Cinv(m_nWires, m_sigmat, ifail);
        if (ifail != 0) {
          std::cerr << m_className << "::PrepareWireSignals:\n";
          std::cerr << "    Inversion of signal matrix (" << mx << ", " << my
                    << ") failed.\n";
          std::cerr << "    No reliable results.\n";
          std::cerr << "    Preparation of weighting fields is abandoned.\n";
          return false;
        }
      }
      // Store the matrix back.
      if (fperx || fpery) {
        // CALL IONIO(MX,MY,1,0,IFAIL)
        // IF(IFAIL.NE.0)GOTO 2010
      }
      // Next layer.
    }
  }

  // And transform the matrices back to the original domain.
  if ((fperx && !fpery) || (fpery && !fperx)) {
    for (unsigned int i = 0; i < m_nWires; ++i) {
      for (int m = -nFourier / 2; m < nFourier / 2; ++m) {
        // CALL IONIO(M,M,2,I,IFAIL)
        // IF(IFAIL.NE.0)GOTO 2010
        for (unsigned int j = 0; j < m_nWires; ++j) {
          fftmat[m + nFourier / 2][j] = m_sigmat[i][j];
        }
      }
      for (unsigned int j = 0; j < m_nWires; ++j) {
        // CALL CFFT(FFTMAT(1,J),-MFEXP)
      }
      for (int m = -nFourier / 2; m < nFourier / 2; ++m) {
        // CALL IONIO(M,M,2,I,IFAIL)
        // IF(IFAIL.NE.0)GOTO 2010
        for (unsigned int j = 0; j < m_nWires; ++j) {
          m_sigmat[i][j] = fftmat[m + nFourier / 2][j] / double(nFourier);
        }
        // CALL IONIO(M,M,1,I,IFAIL)
        // IF(IFAIL.NE.0)GOTO 2010
      }
    }
  }
  // Have them transformed to the original domain (doubly periodic).
  if (fperx && fpery) {
    for (unsigned int i = 0; i < m_nWires; ++i) {
      for (int mx = mxmin; mx <= mxmax; ++mx) {
        for (int my = mymin; my <= mymax; ++my) {
          // CALL IONIO(MX,MY,2,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
          for (unsigned int j = 0; j < m_nWires; ++j) {
            fftmat[my + nFourier / 2 - 1][j] = m_sigmat[i][j];
          }
        }
        for (unsigned int j = 0; j < m_nWires; ++j) {
          // CFFT(FFTMAT(1,J),-MFEXP)
        }
        for (int my = mymin; my <= mymax; ++my) {
          // CALL IONIO(MX,MY,2,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
          for (unsigned int j = 0; j < m_nWires; ++j) {
            m_sigmat[i][j] =
                fftmat[my + nFourier / 2 - 1][j] / double(nFourier);
          }
          // CALL IONIO(MX,MY,1,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
        }
      }
      for (int my = mymin; my <= mymax; ++my) {
        for (int mx = mxmin; mx <= mxmax; ++mx) {
          // CALL IONIO(MX,MY,2,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
          for (unsigned int j = 0; j < m_nWires; ++j) {
            fftmat[mx + nFourier / 2 - 1][j] = m_sigmat[i][j];
          }
        }
        for (unsigned int j = 0; j < m_nWires; ++j) {
          // CALL CFFT(FFTMAT(1,J),-MFEXP)
        }
        for (int mx = mxmin; mx <= mxmax; ++mx) {
          // CALL IONIO(MX,MY,2,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
          for (unsigned int j = 0; j < m_nWires; ++j) {
            m_sigmat[i][j] =
                fftmat[mx + nFourier / 2 - 1][j] / double(nFourier);
          }
          // CALL IONIO(MX,MY,1,I,IFAIL)
          // IF(IFAIL.NE.0)GOTO 2010
        }
      }
    }
  }

  // Dump the signal matrix after inversion, if DEBUG is requested.
  if (m_debug) {
    for (int mx = mxmin; mx <= mxmax; ++mx) {
      for (int my = mymin; my <= mymax; ++my) {
        std::cout << m_className << "::SetupWireSignals:\n";
        std::cout << "    Dump of signal matrix (" << mx << ", " << my
                  << ") after inversion:\n";
        for (unsigned int i = 0; i < m_nWires; i += 10) {
          for (unsigned int j = 0; j < m_nWires; j += 10) {
            std::cout << "    (Re-Block " << i / 10 << ", " << j / 10 << ")\n";
            for (unsigned int ii = 0; ii < 10; ++ii) {
              if (i + ii >= m_nWires) break;
              for (unsigned int jj = 0; jj < 10; ++jj) {
                if (j + jj >= m_nWires) break;
                std::cout << real(m_sigmat[i + ii][j + jj]) << "  ";
              }
              std::cout << "\n";
            }
            std::cout << "\n";
            std::cout << "    (Im-Block " << i / 10 << ", " << j / 10 << ")\n";
            for (int ii = 0; ii < 10; ++ii) {
              if (i + ii >= m_nWires) break;
              for (int jj = 0; jj < 10; ++jj) {
                if (j + jj >= m_nWires) break;
                std::cout << imag(m_sigmat[i + ii][j + jj]) << "  ";
              }
              std::cout << "\n";
            }
            std::cout << "\n";
          }
        }
        std::cout << m_className << "::SetupWireSignals:\n";
        std::cout << "    End of the inverted capacitance matrix dump.\n";
      }
    }
  }
  return true;
}

bool ComponentAnalyticField::SetupPlaneSignals() {

  //-----------------------------------------------------------------------
  //   SIGPLP - Computes the weighting field charges for the planes and
  //            the tube.
  //   (Last changed on 14/10/99.)
  //-----------------------------------------------------------------------

  const int nPlanes = 5;
  m_qplane.assign(nPlanes, std::vector<double>(m_nWires, 0.));

  double vw;

  // Loop over the signal layers.
  for (int mx = mxmin; mx <= mxmax; ++mx) {
    for (int my = mymin; my <= mymax; ++my) {
      // Load the layers of the signal matrices.
      // CALL IONIO(MX,MY,2,0,IFAIL1)
      // IF(IFAIL1.NE.0)THEN
      //   PRINT *,' !!!!!! SIGPLP WARNING : Signal matrix'//
      //           ' store error; field for planes not prepared.'
      //   RETURN
      // ENDIF
      // Initialise the plane matrices.
      m_qplane.assign(nPlanes, std::vector<double>(m_nWires, 0.));
      // Charges for plane 1, if present.
      if (m_ynplan[0]) {
        // Set the weighting field voltages.
        for (unsigned int i = 0; i < m_nWires; ++i) {
          if (m_ynplan[1]) {
            vw = -(m_coplan[1] - m_w[i].x) / (m_coplan[1] - m_coplan[0]);
          } else if (m_perx) {
            vw = -(m_coplan[0] + m_sx - m_w[i].x) / m_sx;
          } else {
            vw = -1;
          }
          // Multiply with the matrix.
          for (unsigned int j = 0; j < m_nWires; ++j) {
            m_qplane[0][j] += real(m_sigmat[i][j]) * vw;
          }
        }
      }
      // Charges for plane 2, if present.
      if (m_ynplan[1]) {
        // Set the weighting field voltages.
        for (unsigned int i = 0; i < m_nWires; ++i) {
          if (m_ynplan[0]) {
            vw = -(m_coplan[0] - m_w[i].x) / (m_coplan[0] - m_coplan[1]);
          } else if (m_perx) {
            vw = -(m_w[i].x - m_coplan[1] + m_sx) / m_sx;
          } else {
            vw = -1.;
          }
          // Multiply with the matrix.
          for (unsigned int j = 0; j < m_nWires; ++j) {
            m_qplane[1][j] += real(m_sigmat[i][j]) * vw;
          }
        }
      }
      // Charges for plane 3, if present.
      if (m_ynplan[2]) {
        // Set the weighting field voltages.
        for (unsigned int i = 0; i < m_nWires; ++i) {
          if (m_ynplan[3]) {
            vw = -(m_coplan[3] - m_w[i].y) / (m_coplan[3] - m_coplan[2]);
          } else if (m_pery) {
            vw = -(m_coplan[2] + m_sy - m_w[i].y) / m_sy;
          } else {
            vw = -1.;
          }
          // Multiply with the matrix.
          for (unsigned int j = 0; j < m_nWires; ++j) {
            m_qplane[2][i] += real(m_sigmat[i][j]) * vw;
          }
        }
      }
      // Charges for plane 4, if present.
      if (m_ynplan[3]) {
        // Set the weighting field voltages.
        for (unsigned int i = 0; i < m_nWires; ++i) {
          if (m_ynplan[2]) {
            vw = -(m_coplan[2] - m_w[i].y) / (m_coplan[2] - m_coplan[3]);
          } else if (m_pery) {
            vw = -(m_w[i].y - m_coplan[3] + m_sy) / m_sy;
          } else {
            vw = -1.;
          }
          // Multiply with the matrix.
          for (unsigned int j = 0; j < m_nWires; ++j) {
            m_qplane[3][i] += real(m_sigmat[i][j]) * vw;
          }
        }
      }
      // Charges for the tube, if present.
      if (m_tube) {
        for (unsigned int i = 0; i < m_nWires; ++i) {
          for (unsigned int j = 0; j < m_nWires; ++j) {
            m_qplane[4][i] -= real(m_sigmat[i][j]);
          }
        }
      }
      // Store the plane charges.
      // CALL IPLIO(MX,MY,1,IFAIL1)
      // IF(IFAIL1.NE.0)THEN
      //   PRINT *,' !!!!!! SIGPLP WARNING : Plane matrix'//
      //           ' store error; field for planes not prepared.'
      //   RETURN
      // ENDIF
      // Next layer.
    }
  }
  // Compute the background weighting fields, first in x.
  if (m_ynplan[0] && m_ynplan[1]) {
    planes[0].ewxcor = 1. / (m_coplan[1] - m_coplan[0]);
    planes[1].ewxcor = 1. / (m_coplan[0] - m_coplan[1]);
  } else if (m_ynplan[0] && m_perx) {
    planes[0].ewxcor = 1. / m_sx;
    planes[1].ewxcor = 0.;
  } else if (m_ynplan[1] && m_perx) {
    planes[0].ewxcor = 0.;
    planes[1].ewxcor = -1. / m_sx;
  } else {
    planes[0].ewxcor = planes[1].ewxcor = 0.;
  }
  planes[2].ewxcor = planes[3].ewxcor = planes[4].ewxcor = 0.;
  // Next also in y.
  planes[0].ewycor = planes[1].ewycor = 0.;
  if (m_ynplan[2] && m_ynplan[3]) {
    planes[2].ewycor = 1. / (m_coplan[3] - m_coplan[2]);
    planes[3].ewycor = 1. / (m_coplan[2] - m_coplan[3]);
  } else if (m_ynplan[2] && m_pery) {
    planes[2].ewycor = 1. / m_sy;
    planes[3].ewycor = 0.;
  } else if (m_ynplan[3] && m_pery) {
    planes[2].ewycor = 0.;
    planes[3].ewycor = -1. / m_sy;
  } else {
    planes[2].ewycor = planes[3].ewycor = 0.;
  }
  // The tube has no correction field.
  planes[4].ewycor = 0.;

  // Debugging output.
  if (m_debug) {
    std::cout << m_className << "::SetupPlaneSignals:\n";
    std::cout << "    Charges for currents induced in the planes:\n";
    std::cout << "    Wire        x-Plane 1        x-Plane 2"
              << "        y-Plane 1        y-Plane 2"
              << "        Tube\n";
    for (unsigned int i = 0; i < m_nWires; ++i) {
      std::cout << "    " << i << "  " << m_qplane[0][i] << "    "
                << m_qplane[1][i] << "    " << m_qplane[2][i] << "    "
                << m_qplane[3][i] << "    " << m_qplane[4][i] << "\n";
    }
    std::cout << m_className << "::SetupPlaneSignals:\n";
    std::cout << "    Bias fields:\n";
    std::cout << "    Plane    x-Bias [1/cm]    y-Bias [1/cm]\n";
    for (int i = 0; i < 4; ++i) {
      std::cout << "    " << i << "  " << planes[i].ewxcor << "  "
                << planes[i].ewycor << "\n";
    }
  }

  return true;
}

bool ComponentAnalyticField::IprA00(const int mx, const int my) {

  //-----------------------------------------------------------------------
  //   IPRA00 - Routine filling the (MX,MY) th layer of the signal matrix
  //            for cells with non-periodic type A (see SIGIPR).
  //-----------------------------------------------------------------------

  const double dx = mx * m_sx;
  const double dy = my * m_sy;
  double aa = 0.;

  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Diagonal terms.
    if (dx != 0. || dy != 0.) {
      aa = dx * dx + dy * dy;
    } else {
      aa = 0.25 * m_w[i].d * m_w[i].d;
    }
    // Take care of single equipotential planes.
    if (m_ynplax) aa /= 2. * pow(m_w[i].x - m_coplax, 2) + dy * dy;
    if (m_ynplay) aa /= 2. * pow(m_w[i].y - m_coplay, 2) + dx * dx;
    // Take care of pairs of equipotential planes.
    if (m_ynplax && m_ynplay)
      aa *= 4. * (pow(m_w[i].x - m_coplax, 2) + pow(m_w[i].y - m_coplay, 2));
    // Define the final version of a[i][i].
    m_sigmat[i][i] = -0.5 * log(aa);
    for (unsigned int j = i + 1; j < m_nWires; ++j) {
      aa = pow(m_w[i].x + dx - m_w[j].x, 2) + pow(m_w[i].y + dy - m_w[j].y, 2);
      // Take care of single planes.
      if (m_ynplax)
        aa /= pow(2. * m_coplax - m_w[i].x - dx - m_w[j].x, 2) +
              pow(m_w[i].y + dy - m_w[j].y, 2);
      if (m_ynplay)
        aa /= pow(m_w[i].x + dx - m_w[j].x, 2) +
              pow(2. * m_coplay - m_w[i].y - dy - m_w[j].y, 2);
      // Take care of pairs of planes.
      if (m_ynplax && m_ynplay) {
        aa *= pow(2. * m_coplax - m_w[i].x - dx - m_w[j].x, 2) +
              pow(2. * m_coplay - m_w[i].y - dy - m_w[j].y, 2);
      }
      // Store the true versions after taking LOGs and SQRT's.
      m_sigmat[i][j] = -0.5 * log(aa);
      m_sigmat[j][i] = m_sigmat[i][j];
    }
  }
  return true;
}

bool ComponentAnalyticField::IprB2X(const int my) {

  //-----------------------------------------------------------------------
  //   IPRB2X - Routine filling the MY th layer of the signal matrix
  //            for cells with non-periodic type B2X (see SIGIPR).
  //   (Last changed on 26/ 4/92.)
  //-----------------------------------------------------------------------

  b2sin.resize(m_nWires);

  const double dy = my * m_sy;
  double aa = 0.;
  double xxneg;

  // Loop over all wires and calculate the diagonal elements first.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    double xx = (Pi / m_sx) * (m_w[i].x - m_coplan[0]);
    if (dy != 0.) {
      aa = pow(sinh(Pi * dy / m_sx) / sin(xx), 2);
    } else {
      aa = pow((0.25 * m_w[i].d * Pi / m_sx) / sin(xx), 2);
    }
    // Take care of a planes at constant y (no dy in this case).
    if (m_ynplay) {
      const double yymirr = (Pi / m_sx) * (m_w[i].y - m_coplay);
      if (fabs(yymirr) <= 20.) {
        const double sinhy = sinh(yymirr);
        const double sinxx = sin(xx);
        aa *= (sinhy * sinhy + sinxx * sinxx) / (sinhy * sinhy);
      }
    }
    // Store the true value of A[i][i].
    m_sigmat[i][i] = -0.5 * log(aa);
    // Loop over all other wires to obtain off-diagonal elements.
    for (unsigned int j = i + 1; j < m_nWires; ++j) {
      const double yy = HalfPi * (m_w[i].y + dy - m_w[j].y) / m_sx;
      xx = HalfPi * (m_w[i].x - m_w[j].x) / m_sx;
      xxneg = HalfPi * (m_w[i].x + m_w[j].x - 2. * m_coplan[0]) / m_sx;
      if (fabs(yy) <= 20.) {
        const double sinhy = sinh(yy);
        const double sinxx = sin(xx);
        const double sinxxneg = sin(xxneg);
        aa = (sinhy * sinhy + sinxx * sinxx) /
             (sinhy * sinhy + sinxxneg * sinxxneg);
      } else {
        aa = 1.;
      }
      // Take equipotential planes into account (no dy anyhow).
      if (m_ynplay) {
        const double yymirr =
            HalfPi * (m_w[i].y + m_w[j].y - 2. * m_coplay) / m_sx;
        if (fabs(yymirr) <= 20.) {
          const double sinhy = sinh(yymirr);
          const double sinxx = sin(xx);
          const double sinxxneg = sin(xxneg);
          aa *= (sinhy * sinhy + sinxxneg * sinxxneg) /
                (sinhy * sinhy + sinxx * sinxx);
        }
      }
      // Store the true value of a[i][j] in both a[i][j] and a[j][i].
      m_sigmat[i][j] = -0.5 * log(aa);
      m_sigmat[j][i] = m_sigmat[i][j];
    }
    // Fill the B2SIN vector.
    b2sin[i] = sin(Pi * (m_coplan[0] - m_w[i].x) / m_sx);
  }

  return true;
}

bool ComponentAnalyticField::IprB2Y(const int mx) {

  //-----------------------------------------------------------------------
  //   IPRB2Y - Routine filling the MX th layer of the signal matrix
  //            for cells with non-periodic type B2Y (see SIGIPR).
  //   (Last changed on 26/ 4/92.)
  //-----------------------------------------------------------------------

  b2sin.resize(m_nWires);

  const double dx = mx * m_sx;
  double aa = 0.;
  double xx, yy, xxmirr, yyneg;

  // Loop over all wires and calculate the diagonal elements first.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    yy = (Pi / m_sy) * (m_w[i].y - m_coplan[2]);
    if (dx != 0.) {
      aa = pow(sinh(Pi * dx / m_sy) / sin(yy), 2);
    } else {
      aa = pow((0.25 * m_w[i].d * Pi / m_sy) / sin(yy), 2);
    }
    // Take care of a planes at constant x (no dx in this case).
    if (m_ynplax) {
      xxmirr = (Pi / m_sy) * (m_w[i].x - m_coplax);
      if (fabs(xxmirr) <= 20.) {
        aa *= (pow(sinh(xxmirr), 2) + pow(sin(yy), 2)) / pow(sinh(xxmirr), 2);
      }
    }
    // Store the true value of A[i][i].
    m_sigmat[i][i] = -0.5 * log(aa);
    // Loop over all other wires to obtain off-diagonal elements.
    for (unsigned int j = i + 1; j < m_nWires; ++j) {
      xx = HalfPi * (m_w[i].x + dx - m_w[j].x) / m_sy;
      yy = HalfPi * (m_w[i].y - m_w[j].y) / m_sy;
      yyneg = HalfPi * (m_w[i].y + m_w[j].y - 2. * m_coplan[2]) / m_sy;
      if (fabs(xx) <= 20.) {
        aa = (pow(sinh(xx), 2) + pow(sin(yy), 2)) /
             (pow(sinh(xx), 2) + pow(sin(yyneg), 2));
      } else {
        aa = 1.;
      }
      // Take equipotential planes into account (no dx anyhow).
      if (m_ynplax) {
        xxmirr = HalfPi * (m_w[i].x + m_w[j].x - 2. * m_coplax) / m_sy;
        if (fabs(xxmirr) <= 20.) {
          aa *= (pow(sinh(xxmirr), 2) + pow(sin(yyneg), 2)) /
                (pow(sinh(xxmirr), 2) + pow(sin(yy), 2));
        }
      }
      // Store the true value of a[i][j] in both a[i][j] and a[j][i].
      m_sigmat[i][j] = -0.5 * log(aa);
      m_sigmat[j][i] = m_sigmat[i][j];
    }
    // Fill the B2SIN vector.
    b2sin[i] = sin(Pi * (m_coplan[2] - m_w[i].y) / m_sy);
  }
  return true;
}

bool ComponentAnalyticField::IprC2X() {

  //-----------------------------------------------------------------------
  //   IPRC2X - This initializing subroutine stores the capacitance matrix
  //            for the configuration:
  //            wires at zw(j)+cmplx(lx*2*sx,ly*sy),
  //            j=1(1)n, lx=-infinity(1)infinity, ly=-infinity(1)infinity.
  //            but the signs of the charges alternate in the x-direction
  //   (Last changed on  4/10/06.)
  //-----------------------------------------------------------------------

  // Fill the capacitance matrix.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    double cx = m_coplax - m_sx * int(round((m_coplax - m_w[i].x) / m_sx));
    for (unsigned int j = 0; j < m_nWires; ++j) {
      double temp = 0.;
      if (mode == 0) {
        temp = (m_w[i].x - cx) * (m_w[j].x - cx) * TwoPi / (m_sx * m_sy);
      }
      if (i == j) {
        m_sigmat[i][j] =
            Ph2Lim(0.5 * m_w[i].d) - Ph2(2. * (m_w[j].x - cx), 0.) - temp;
      } else {
        m_sigmat[i][j] =
            Ph2(m_w[i].x - m_w[j].x, m_w[i].y - m_w[j].y) -
            Ph2(m_w[i].x + m_w[j].x - 2. * cx, m_w[i].y - m_w[j].y) - temp;
      }
    }
  }
  return true;
}

bool ComponentAnalyticField::IprC2Y() {

  //-----------------------------------------------------------------------
  //   IPRC2Y - This initializing subroutine stores the capacitance matrix
  //            for the configuration:
  //            wires at zw(j)+cmplx(lx*sx,ly*2*sy),
  //            j=1(1)n, lx=-infinity(1)infinity, ly=-infinity(1)infinity.
  //            but the signs of the charges alternate in the y-direction
  //   (Last changed on  4/10/06.)
  //-----------------------------------------------------------------------

  // Fill the capacitance matrix.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    const double cy = m_coplay - m_sy * int(round((m_coplay - m_w[i].y) / m_sy));
    for (unsigned int j = 0; j < m_nWires; ++j) {
      double temp = 0.;
      if (mode == 1) {
        temp = (m_w[i].y - cy) * (m_w[j].y - cy) * TwoPi / (m_sx * m_sy);
      }
      if (i == j) {
        m_sigmat[i][j] =
            Ph2Lim(0.5 * m_w[i].d) - Ph2(0., 2. * (m_w[j].y - cy)) - temp;
      } else {
        m_sigmat[i][j] =
            Ph2(m_w[i].x - m_w[j].x, m_w[i].y - m_w[j].y) -
            Ph2(m_w[i].x - m_w[j].x, m_w[i].y + m_w[j].y - 2. * cy) - temp;
      }
    }
  }
  return true;
}

bool ComponentAnalyticField::IprC30() {

  //-----------------------------------------------------------------------
  //   IPRC30 - Routine filling the signal matrix for cells of type C30.
  //            Since the signal matrix equals the capacitance matrix for
  //            this potential, the routine is identical to SETC30 except
  //            for the C and P parameters.
  //   (Last changed on 11/11/97.)
  //-----------------------------------------------------------------------

  // Fill the capacitance matrix.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    const double cx = m_coplax - m_sx * int(round((m_coplax - m_w[i].x) / m_sx));
    const double cy = m_coplay - m_sy * int(round((m_coplay - m_w[i].y) / m_sy));
    for (unsigned int j = 0; j < m_nWires; ++j) {
      if (i == j) {
        m_sigmat[i][i] = Ph2Lim(0.5 * m_w[i].d) -
                         Ph2(0., 2. * (m_w[i].y - cy)) -
                         Ph2(2. * (m_w[i].x - cx), 0.) +
                         Ph2(2. * (m_w[i].x - cx), 2. * (m_w[i].y - cy));
      } else {
        m_sigmat[i][j] =
            Ph2(m_w[i].x - m_w[j].x, m_w[i].y - m_w[j].y) -
            Ph2(m_w[i].x - m_w[j].x, m_w[i].y + m_w[j].y - 2. * cy) -
            Ph2(m_w[i].x + m_w[j].x - 2. * cx, m_w[i].y - m_w[j].y) +
            Ph2(m_w[i].x + m_w[j].x - 2. * cx, m_w[i].y + m_w[j].y - 2. * cy);
      }
    }
  }
  return true;
}

bool ComponentAnalyticField::IprD10() {

  //-----------------------------------------------------------------------
  //   IPRD10 - Signal matrix preparation for D1 cells.
  //   VARIABLES :
  //   (Last changed on  2/ 2/93.)
  //-----------------------------------------------------------------------

  const double r2 = m_cotube * m_cotube;
  // Loop over all wires.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Set the diagonal terms.
    m_sigmat[i][i] =
        -log(0.5 * m_w[i].d /
             (m_cotube -
              (m_w[i].x * m_w[i].x + m_w[i].y * m_w[i].y) / m_cotube));
    // Set a complex wire-coordinate to make things a little easier.
    std::complex<double> zi(m_w[i].x, m_w[i].y);
    // Loop over all other wires for the off-diagonal elements.
    for (unsigned int j = i + 1; j < m_nWires; ++j) {
      // Set a complex wire-coordinate to make things a little easier.
      std::complex<double> zj(m_w[j].x, m_w[j].y);
      m_sigmat[i][j] =
          -log(abs((1. / m_cotube) * (zi - zj) / (1. - conj(zi) * zj / r2)));
      // Copy this to a[j][i] since the capacitance matrix is symmetric.
      m_sigmat[j][i] = m_sigmat[i][j];
    }
  }
  return true;
}

bool ComponentAnalyticField::IprD30() {

  //-----------------------------------------------------------------------
  //   IPRD30 - Signal matrix preparation for polygonal cells (type D3).
  //   Variables :
  //   (Last changed on 19/ 6/97.)
  //-----------------------------------------------------------------------

  wmap.resize(m_nWires);

  std::complex<double> wd;
  InitializeCoefficientTables();

  // Loop over all wire combinations.
  for (int i = 0; i < int(m_nWires); ++i) {
    // We need to compute the wire mapping again to obtain WD.
    ConformalMap(std::complex<double>(m_w[i].x, m_w[i].y) / m_cotube, wmap[i],
                 wd);
    // Diagonal elements.
    m_sigmat[i][i] = -log(abs((0.5 * m_w[i].d / m_cotube) * wd /
                              (1. - pow(abs(wmap[i]), 2))));
    // Loop over all other wires for the off-diagonal elements.
    for (int j = 0; j < i - 1; ++j) {
      m_sigmat[i][j] =
          -log(abs((wmap[i] - wmap[j]) / (1. - conj(wmap[i]) * wmap[j])));
      // Copy this to a[j][i] since the capacitance matrix is symmetric.
      m_sigmat[j][i] = m_sigmat[i][j];
    }
  }
  return true;
}

bool ComponentAnalyticField::Wfield(const double xpos, const double ypos,
                                    const double zpos, double& exsum,
                                    double& eysum, double& ezsum, double& vsum,
                                    const int isw, const bool opt) const {

  //-----------------------------------------------------------------------
  //   SIGFLS - Sums the weighting field components at (XPOS,YPOS,ZPOS).
  //   (Last changed on 11/10/06.)
  //-----------------------------------------------------------------------

  // Initialise the sums.
  exsum = eysum = ezsum = vsum = 0.;
  double ex = 0., ey = 0., ez = 0.;
  double volt = 0.;
  if (!m_sigset) return false;

  // Loop over the signal layers.
  for (int mx = mxmin; mx <= mxmax; ++mx) {
    for (int my = mymin; my <= mymax; ++my) {
      // Load the layers of the wire matrices.
      // CALL IONIO(MX,MY,2,0,IFAIL)
      // if (!LoadWireLayers(mx, my)) {
      //  std::cerr << m_className << "::LoadWireLayers:\n";
      //  std::cerr << "    Wire matrix store error.\n";
      //  std::cerr << "    No weighting field returned.\n";
      //  exsum = eysum = ezsum = 0.;
      //  return false;
      //}
      // Loop over all wires.
      for (int iw = m_nWires; iw--;) {
        // Pick out those wires that are part of this read out group.
        if (m_w[iw].ind == isw) {
          ex = ey = ez = 0.;
          if (m_scellTypeFourier == "A  ") {
            WfieldWireA00(xpos, ypos, ex, ey, volt, mx, my, iw, opt);
          } else if (m_scellTypeFourier == "B2X") {
            WfieldWireB2X(xpos, ypos, ex, ey, volt, my, iw, opt);
          } else if (m_scellTypeFourier == "B2Y") {
            WfieldWireB2Y(xpos, ypos, ex, ey, volt, mx, iw, opt);
          } else if (m_scellTypeFourier == "C2X") {
            WfieldWireC2X(xpos, ypos, ex, ey, volt, iw, opt);
          } else if (m_scellTypeFourier == "C2Y") {
            WfieldWireC2Y(xpos, ypos, ex, ey, volt, iw, opt);
          } else if (m_scellTypeFourier == "C3 ") {
            WfieldWireC30(xpos, ypos, ex, ey, volt, iw, opt);
          } else if (m_scellTypeFourier == "D1 ") {
            WfieldWireD10(xpos, ypos, ex, ey, volt, iw, opt);
          } else if (m_scellTypeFourier == "D3 ") {
            WfieldWireD30(xpos, ypos, ex, ey, volt, iw, opt);
          } else {
            std::cerr << m_className << "::Wfield:\n";
            std::cerr << "    Unknown signal field type " << m_scellTypeFourier
                      << " received. Program error!\n";
            std::cerr << "    Encountered for wire " << iw
                      << ", readout group = " << m_w[iw].ind << "\n";
            exsum = eysum = ezsum = vsum = 0.;
            return false;
          }
          exsum += ex;
          eysum += ey;
          ezsum += ez;
          if (opt) vsum += volt;
        }
      }
      // Load the layers of the plane matrices.
      // CALL IPLIO(MX,MY,2,IFAIL)
      // if (!LoadPlaneLayers(mx, my)) {
      // std::cerr << m_className << "::Wfield:\n";
      //  std::cerr << "    Plane matrix store error.\n";
      //  std::cerr << "    No weighting field returned.\n";
      //  exsum = eysum = ezsum = 0.;
      //  return;
      //}
      // Loop over all planes.
      for (int ip = 0; ip < 5; ++ip) {
        // Pick out those that are part of this read out group.
        if (planes[ip].ind == isw) {
          ex = ey = ez = 0.;
          if (m_scellTypeFourier == "A  ") {
            WfieldPlaneA00(xpos, ypos, ex, ey, volt, mx, my, ip, opt);
          } else if (m_scellTypeFourier == "B2X") {
            WfieldPlaneB2X(xpos, ypos, ex, ey, volt, my, ip, opt);
          } else if (m_scellTypeFourier == "B2Y") {
            WfieldPlaneB2Y(xpos, ypos, ex, ey, volt, mx, ip, opt);
          } else if (m_scellTypeFourier == "C2X") {
            WfieldPlaneC2X(xpos, ypos, ex, ey, volt, ip, opt);
          } else if (m_scellTypeFourier == "C2Y") {
            WfieldPlaneC2Y(xpos, ypos, ex, ey, volt, ip, opt);
          } else if (m_scellTypeFourier == "D1 ") {
            WfieldPlaneD10(xpos, ypos, ex, ey, volt, ip, opt);
          } else if (m_scellTypeFourier == "D3 ") {
            WfieldPlaneD30(xpos, ypos, ex, ey, volt, ip, opt);
          } else {
            std::cerr << m_className << "::Wfield:\n";
            std::cerr << "    Unkown field type " << m_scellTypeFourier
                      << " received. Program error!\n";
            std::cerr << "    Encountered for plane " << ip
                      << ", readout group = " << planes[ip].ind << "\n";
            exsum = eysum = ezsum = 0.;
            return false;
          }
          exsum += ex;
          eysum += ey;
          ezsum += ez;
          if (opt) vsum += volt;
        }
      }
      // Next signal layer.
    }
  }
  // Add the field due to the planes themselves.
  for (int ip = 0; ip < 5; ++ip) {
    if (planes[ip].ind == isw) {
      exsum += planes[ip].ewxcor;
      eysum += planes[ip].ewycor;
      if (opt) {
        if (ip == 0 || ip == 1) {
          double xx = xpos;
          if (m_perx) {
            xx -= m_sx * int(round(xpos / m_sx));
            if (m_ynplan[0] && xx <= m_coplan[0]) xx += m_sx;
            if (m_ynplan[1] && xx >= m_coplan[1]) xx -= m_sx;
          }
          vsum += 1. - planes[ip].ewxcor * (xx - m_coplan[ip]);
        } else if (ip == 2 || ip == 3) {
          double yy = ypos;
          if (m_pery) {
            yy -= m_sy * int(round(ypos / m_sy));
            if (m_ynplan[2] && yy <= m_coplan[2]) yy += m_sy;
            if (m_ynplan[3] && yy >= m_coplan[3]) yy -= m_sy;
          }
          vsum += 1. - planes[ip].ewycor * (yy - m_coplan[ip]);
        }
      }
    }
  }

  // Add strips and pixels, if there are any.
  for (unsigned int ip = 0; ip < 5; ++ip) {
    const unsigned int nStrips1 = planes[ip].strips1.size();
    for (unsigned int istrip = 0; istrip < nStrips1; ++istrip) {
      if (planes[ip].strips1[istrip].ind == isw) {
        WfieldStripXy(xpos, ypos, zpos, ex, ey, ez, volt, ip, istrip, opt);
        exsum += ex;
        eysum += ey;
        ezsum += ez;
        if (opt) vsum += volt;
      }
    }
    const unsigned int nStrips2 = planes[ip].strips2.size();
    for (unsigned int istrip = 0; istrip < nStrips2; ++istrip) {
      if (planes[ip].strips2[istrip].ind == isw) {
        WfieldStripZ(xpos, ypos, ex, ey, volt, ip, istrip, opt);
        exsum += ex;
        eysum += ey;
        if (opt) vsum += volt;
      }
    }
    const unsigned int nPixels = planes[ip].pixels.size();
    for (unsigned int ipix = 0; ipix < nPixels; ++ipix) {
      if (planes[ip].pixels[ipix].ind != isw) continue;
      WfieldPixel(xpos, ypos, zpos, ex, ey, ez, volt, ip, ipix, opt);
      exsum += ex;
      eysum += ey;
      ezsum += ez;
      if (opt) vsum += volt;
    }
  }
  return true;
}

void ComponentAnalyticField::WfieldWireA00(const double xpos, const double ypos,
                                           double& ex, double& ey, double& volt,
                                           const int mx, const int my,
                                           const int isw, const bool opt) const {

  //-----------------------------------------------------------------------
  //   IONA00 - Routine returning the A I,J [MX,MY] * E terms for A cells.
  //   VARIABLES : R2         : Potential before taking -Log(Sqrt(...))
  //               EX,EY      : x,y-Component of the electric field.
  //               ETOT       : Magnitude of the electric field.
  //               VOLT       : Potential.
  //               EXHELP ETC : One term in the summing series.
  //               (XPOS,YPOS): Position where the field is needed.
  //   (Last changed on 14/ 8/98.)
  //-----------------------------------------------------------------------

  // Initialise the electric field and potential.
  ex = ey = volt = 0.;

  double xxmirr = 0., yymirr = 0.;
  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    // Define a few reduced variables.
    const double xx = xpos - m_w[i].x - mx * m_sx;
    const double yy = ypos - m_w[i].y - my * m_sy;
    // Calculate the field in case there are no planes.
    double r2 = xx * xx + yy * yy;
    if (r2 <= 0.) continue;
    double exhelp = xx / r2;
    double eyhelp = yy / r2;
    // Take care of a plane at constant x.
    if (m_ynplax) {
      xxmirr = xpos + m_w[i].x - 2. * m_coplax;
      const double r2plan = xxmirr * xxmirr + yy * yy;
      if (r2plan <= 0.) continue;
      exhelp -= xxmirr / r2plan;
      eyhelp -= yy / r2plan;
      r2 /= r2plan;
    }
    // Take care of a plane at constant y.
    if (m_ynplay) {
      yymirr = ypos + m_w[i].y - 2. * m_coplay;
      const double r2plan = xx * xx + yymirr * yymirr;
      if (r2plan <= 0.) continue;
      exhelp -= xx / r2plan;
      eyhelp -= yymirr / r2plan;
      r2 /= r2plan;
    }
    // Take care of pairs of planes.
    if (m_ynplax && m_ynplay) {
      const double r2plan = xxmirr * xxmirr + yymirr * yymirr;
      if (r2plan <= 0.) continue;
      exhelp += xxmirr / r2plan;
      eyhelp += yymirr / r2plan;
      r2 *= r2plan;
    }
    // Calculate the electric field and the potential.
    if (opt) volt -= 0.5 * real(m_sigmat[isw][i]) * log(r2);
    ex += real(m_sigmat[isw][i]) * exhelp;
    ey += real(m_sigmat[isw][i]) * eyhelp;
  }
}

void ComponentAnalyticField::WfieldWireB2X(const double xpos, const double ypos,
                                           double& ex, double& ey, double& volt,
                                           const int my, const int isw,
                                           const bool opt) const {

  //-----------------------------------------------------------------------
  //   IONB2X - Routine calculating the MY contribution to the signal on
  //            wire ISW due to a charge at (XPOS,YPOS) for F-B2Y cells.
  //   VARIABLES : See routine EFCA00 for most of the variables.
  //               Z,ZZMIRR   : X + I*Y , XXMIRR + I*YYMIRR ; I**2=-1
  //               ECOMPL     : EX + I*EY                   ; I**2=-1
  //   (Last changed on 20/ 2/90.)
  //-----------------------------------------------------------------------

  std::complex<double> zz, ecompl, zzmirr, zzneg, zznmirr;

  // Initialise the electric field and potential.
  ex = ey = volt = 0.;

  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    const double xx = HalfPi * (xpos - m_w[i].x) / m_sx;
    const double yy = HalfPi * (ypos - m_w[i].y - my * m_sy) / m_sx;
    const double xxneg = HalfPi * (xpos + m_w[i].x - 2. * m_coplan[0]) / m_sx;
    zz = std::complex<double>(xx, yy);
    zzneg = std::complex<double>(xxneg, yy);
    // Calculate the field in case there are no equipotential planes.
    ecompl = 0.;
    double r2 = 1.;
    if (fabs(yy) <= 20.) {
      ecompl = -b2sin[i] / (sin(zz) * sin(zzneg));
      if (opt) {
        const double sinhy = sinh(yy);
        const double sinxx = sin(xx);
        const double sinxxneg = sin(xxneg);
        r2 = (sinhy * sinhy + sinxx * sinxx) /
             (sinhy * sinhy + sinxxneg * sinxxneg);
      }
    }
    // Take care of a plane at constant y.
    if (m_ynplay) {
      const double yymirr = (HalfPi / m_sx) * (ypos + m_w[i].y - 2. * m_coplay);
      zzmirr = std::complex<double>(xx, yymirr);
      zznmirr = std::complex<double>(xxneg, yymirr);
      if (fabs(yymirr) <= 20.) {
        ecompl += b2sin[i] / (sin(zzmirr) * sin(zznmirr));
        if (opt) {
          const double sinhy = sinh(yymirr);
          const double sinxx = sin(xx);
          const double sinxxneg = sin(xxneg);
          const double r2plan = (sinhy * sinhy + sinxx * sinxx) /
                                (sinhy * sinhy + sinxxneg * sinxxneg);
          r2 /= r2plan;
        }
      }
    }
    // Calculate the electric field and potential.
    ex += real(m_sigmat[isw][i]) * real(ecompl);
    ey -= real(m_sigmat[isw][i]) * imag(ecompl);
    if (opt) volt -= 0.5 * real(m_sigmat[isw][i]) * log(r2);
  }
  ex *= HalfPi / m_sx;
  ey *= HalfPi / m_sx;
}

void ComponentAnalyticField::WfieldWireB2Y(const double xpos, const double ypos,
                                           double& ex, double& ey, double& volt,
                                           const int mx, const int isw,
                                           const bool opt) const {

  //-----------------------------------------------------------------------
  //   IONB2Y - Routine calculating the MX contribution to the signal on
  //            wire ISW due to a charge at (XPOS,YPOS) for F-B2X cells.
  //   VARIABLES : See routine EFCA00 for most of the variables.
  //               Z,ZZMIRR   : X + I*Y , XXMIRR + I*YYMIRR ; I**2=-1
  //               ECOMPL     : EX + I*EY                   ; I**2=-1
  //   (Last changed on 20/ 2/90.)
  //-----------------------------------------------------------------------

  const std::complex<double> icons = std::complex<double>(0., 1.);

  std::complex<double> zz, ecompl, zzmirr, zzneg, zznmirr;

  // Initialise the electric field and potential.
  ex = ey = volt = 0.;

  // Loop over all wires.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    const double xx = HalfPi * (xpos - m_w[i].x - mx * m_sx) / m_sy;
    const double yy = HalfPi * (ypos - m_w[i].y) / m_sy;
    const double yyneg = HalfPi * (ypos + m_w[i].y - 2. * m_coplan[2]) / m_sy;
    zz = std::complex<double>(xx, yy);
    zzneg = std::complex<double>(xx, yyneg);
    // Calculate the field in case there are no equipotential planes.
    ecompl = 0.;
    double r2 = 1.;
    if (fabs(xx) <= 20.) {
      ecompl = icons * b2sin[i] / (sin(icons * zz) * sin(icons * zzneg));
      if (opt) {
        r2 = (pow(sinh(xx), 2) + pow(sin(yy), 2)) /
             (pow(sinh(xx), 2) + pow(sin(yyneg), 2));
      }
    }
    // Take care of a plane at constant x.
    if (m_ynplax) {
      const double xxmirr = (HalfPi / m_sy) * (xpos + m_w[i].x - 2. * m_coplax);
      zzmirr = std::complex<double>(xxmirr, yy);
      zznmirr = std::complex<double>(xxmirr, yyneg);
      if (fabs(xxmirr) <= 20.) {
        ecompl -=
            icons * b2sin[i] / (sin(icons * zzmirr) * sin(icons * zznmirr));
        if (opt) {
          const double r2plan = (pow(sinh(xxmirr), 2) + pow(sin(yy), 2)) /
                                (pow(sinh(xxmirr), 2) + pow(sin(yyneg), 2));
          r2 /= r2plan;
        }
      }
    }
    // Calculate the electric field and potential.
    ex += real(m_sigmat[isw][i]) * real(ecompl);
    ey -= real(m_sigmat[isw][i]) * imag(ecompl);
    if (opt) volt -= 0.5 * real(m_sigmat[isw][i]) * log(r2);
  }
  ex *= HalfPi / m_sy;
  ey *= HalfPi / m_sy;
}

void ComponentAnalyticField::WfieldWireC2X(const double xpos, const double ypos,
                                           double& ex, double& ey, double& volt,
                                           const int isw, const bool opt) const {

  //-----------------------------------------------------------------------
  //   IONC2X - Routine returning the potential and electric field in a
  //            configuration with 2 x planes and y periodicity.
  //   VARIABLES : see the writeup
  //   (Last changed on 12/10/06.)
  //-----------------------------------------------------------------------

  const std::complex<double> icons = std::complex<double>(0., 1.);
  std::complex<double> zsin, zcof, zu, zunew, zterm1, zterm2, zeta;

  // Initial values.
  std::complex<double> wsum1 = 0.;
  std::complex<double> wsum2 = 0.;
  double s = 0.;
  volt = 0.;

  // Wire loop.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Compute the direct contribution.
    zeta = m_zmult * std::complex<double>(xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum1 -= real(m_sigmat[isw][i]) * icons;
      if (opt) volt -= real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum1 += real(m_sigmat[isw][i]) * icons;
      if (opt) volt -= real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum1 += real(m_sigmat[isw][i]) * (zterm2 / zterm1);
      if (opt) volt -= real(m_sigmat[isw][i]) * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    double cx = m_coplax - m_sx * int(round((m_coplax - m_w[i].x) / m_sx));
    // Constant terms sum
    s += real(m_sigmat[isw][i]) * (m_w[i].x - cx);
    // Mirror contribution.
    zeta = m_zmult *
           std::complex<double>(2. * cx - xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > +15.) {
      wsum2 -= real(m_sigmat[isw][i]) * icons;
      if (opt) volt += real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum2 += real(m_sigmat[isw][i]) * icons;
      if (opt) volt += real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum2 += real(m_sigmat[isw][i]) * (zterm2 / zterm1);
      if (opt) volt += real(m_sigmat[isw][i]) * log(abs(zterm1));
    }
    // Correct the voltage, if needed (MODE).
    if (opt && mode == 0) {
      volt -= TwoPi * real(m_sigmat[isw][i]) * (xpos - cx) * (m_w[i].x - cx) /
              (m_sx * m_sy);
    }
  }
  // Convert the two contributions to a real field.
  ex = real(m_zmult * (wsum1 + wsum2));
  ey = -imag(m_zmult * (wsum1 - wsum2));
  // Constant correction terms.
  if (mode == 0) ex += s * TwoPi / (m_sx * m_sy);
}

void ComponentAnalyticField::WfieldWireC2Y(const double xpos, const double ypos,
                                           double& ex, double& ey, double& volt,
                                           const int isw, const bool opt) const {

  //-----------------------------------------------------------------------
  //   IONC2Y - Routine returning the potential and electric field in a
  //            configuration with 2 y planes and x periodicity.
  //   VARIABLES : see the writeup
  //   (Last changed on 12/10/06.)
  //-----------------------------------------------------------------------

  const std::complex<double> icons = std::complex<double>(0., 1.);
  std::complex<double> zsin, zcof, zu, zunew, zterm1, zterm2, zeta;

  // Initial values.
  std::complex<double> wsum1 = 0.;
  std::complex<double> wsum2 = 0.;
  double s = 0.;
  volt = 0.;

  // Wire loop.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Compute the direct contribution.
    zeta = m_zmult * std::complex<double>(xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > +15.) {
      wsum1 -= real(m_sigmat[isw][i]) * icons;
      if (opt) volt -= real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum1 += real(m_sigmat[isw][i]) * icons;
      if (opt) volt -= real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum1 += real(m_sigmat[isw][i]) * (zterm2 / zterm1);
      if (opt) volt -= real(m_sigmat[isw][i]) * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    double cy = m_coplay - m_sy * int(round((m_coplay - m_w[i].y) / m_sy));
    // Constant terms sum
    s += real(m_sigmat[isw][i]) * (m_w[i].y - cy);
    // Mirror contribution.
    zeta = m_zmult *
           std::complex<double>(xpos - m_w[i].x, 2. * cy - ypos - m_w[i].y);
    if (imag(zeta) > +15.) {
      wsum2 -= real(m_sigmat[isw][i]) * icons;
      if (opt) volt += real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum2 += real(m_sigmat[isw][i]) * icons;
      if (opt) volt += real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum2 += real(m_sigmat[isw][i]) * (zterm2 / zterm1);
      if (opt) volt += real(m_sigmat[isw][i]) * log(abs(zterm1));
    }
    // Correct the voltage, if needed (MODE).
    if (opt && mode == 1) {
      volt -= TwoPi * real(m_sigmat[isw][i]) * (ypos - cy) * (m_w[i].y - cy) /
              (m_sx * m_sy);
    }
  }
  // Convert the two contributions to a real field.
  ex = real(m_zmult * (wsum1 - wsum2));
  ey = -imag(m_zmult * (wsum1 + wsum2));
  // Constant correction terms.
  if (mode == 1) ey += s * TwoPi / (m_sx * m_sy);
}

void ComponentAnalyticField::WfieldWireC30(const double xpos, const double ypos,
                                           double& ex, double& ey, double& volt,
                                           const int isw, const bool opt) const {

  //-----------------------------------------------------------------------
  //   IONC30 - Routine returning the weighting field field in a
  //            configuration with 2 y and 2 x planes. This routine is
  //            basically the same as EFCC30.
  //   (Last changed on 11/11/97.)
  //-----------------------------------------------------------------------

  const std::complex<double> icons = std::complex<double>(0., 1.);
  std::complex<double> zsin, zcof, zu, zunew, zterm1, zterm2, zeta;

  // Initial values.
  std::complex<double> wsum1 = 0.;
  std::complex<double> wsum2 = 0.;
  std::complex<double> wsum3 = 0.;
  std::complex<double> wsum4 = 0.;
  volt = 0.;

  // Wire loop.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Compute the direct contribution.
    zeta = m_zmult * std::complex<double>(xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > +15.) {
      wsum1 -= real(m_sigmat[isw][i]) * icons;
      if (opt) volt -= real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum1 += real(m_sigmat[isw][i]) * icons;
      if (opt) volt -= real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum1 += real(m_sigmat[isw][i]) * (zterm2 / zterm1);
      if (opt) volt -= real(m_sigmat[isw][i]) * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    double cx = m_coplax - m_sx * int(round((m_coplax - m_w[i].x) / m_sx));
    // Mirror contribution from the x plane.
    zeta = m_zmult *
           std::complex<double>(2. * cx - xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > +15.) {
      wsum2 -= real(m_sigmat[isw][i]) * icons;
      if (opt) volt += real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum2 += real(m_sigmat[isw][i]) * icons;
      if (opt) volt += real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum2 += real(m_sigmat[isw][i]) * (zterm2 / zterm1);
      if (opt) volt += real(m_sigmat[isw][i]) * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    double cy = m_coplay - m_sy * int(round((m_coplay - m_w[i].y) / m_sy));
    // Mirror contribution from the y plane.
    zeta = m_zmult *
           std::complex<double>(xpos - m_w[i].x, 2. * cy - ypos - m_w[i].y);
    if (imag(zeta) > +15.) {
      wsum3 -= real(m_sigmat[isw][i]) * icons;
      if (opt) volt += real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum3 += real(m_sigmat[isw][i]) * icons;
      if (opt) volt += real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum3 += real(m_sigmat[isw][i]) * (zterm2 / zterm1);
      if (opt) volt += real(m_sigmat[isw][i]) * log(abs(zterm1));
    }
    // Mirror contribution from both the x and the y plane.
    zeta = m_zmult * std::complex<double>(2. * cx - xpos - m_w[i].x,
                                          2. * cy - ypos - m_w[i].y);
    if (imag(zeta) > +15.) {
      wsum4 -= real(m_sigmat[isw][i]) * icons;
      if (opt) volt -= real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum4 += real(m_sigmat[isw][i]) * icons;
      if (opt) volt -= real(m_sigmat[isw][i]) * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum4 += real(m_sigmat[isw][i]) * (zterm2 / zterm1);
      if (opt) volt -= real(m_sigmat[isw][i]) * log(abs(zterm1));
    }
  }
  // Convert the two contributions to a real field.
  ex = real(m_zmult * (wsum1 + wsum2 - wsum3 - wsum4));
  ey = -imag(m_zmult * (wsum1 - wsum2 + wsum3 - wsum4));
}

void ComponentAnalyticField::WfieldWireD10(const double xpos, const double ypos,
                                           double& ex, double& ey, double& volt,
                                           const int isw, const bool opt) const {

  //-----------------------------------------------------------------------
  //   IOND10 - Subroutine computing the signal on wire ISW due to a charge
  //            at (XPOS,YPOS). This is effectively routine EFCD10.
  //   VARIABLES : EX, EY, VOLT:Electric field and potential.
  //               ETOT, VOLT : Magnitude of electric field, potential.
  //               (XPOS,YPOS): The position where the field is calculated.
  //               ZI, ZPOS   : Shorthand complex notations.
  //   (Last changed on  2/ 2/93.)
  //-----------------------------------------------------------------------

  // Initialise the electric field and potential.
  ex = ey = volt = 0.;

  // Set the complex position coordinates.
  std::complex<double> zpos = std::complex<double>(xpos, ypos);
  std::complex<double> zi;
  std::complex<double> wi;
  const double r2 = m_cotube * m_cotube;
  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    // Set the complex version of the wire-coordinate for simplicity.
    zi = std::complex<double>(m_w[i].x, m_w[i].y);
    // Compute the contribution to the potential, if needed.
    if (opt) {
      volt -= real(m_sigmat[isw][i]) *
              log(abs(m_cotube * (zpos - zi) / (r2 - zpos * conj(zi))));
    }
    // Compute the contribution to the electric field.
    wi = 1. / conj(zpos - zi) + zi / (r2 - conj(zpos) * zi);
    ex += real(m_sigmat[isw][i]) * real(wi);
    ey += real(m_sigmat[isw][i]) * imag(wi);
  }
}

void ComponentAnalyticField::WfieldWireD30(const double xpos, const double ypos,
                                           double& ex, double& ey, double& volt,
                                           const int isw, const bool opt) const {

  //-----------------------------------------------------------------------
  //   IOND30 - Subroutine computing the weighting field for a polygonal
  //            cells without periodicities, type D3.
  //   VARIABLES : EX, EY     :Electric field
  //               (XPOS,YPOS): The position where the field is calculated.
  //               ZI, ZPOS   : Shorthand complex notations.
  //   (Last changed on 19/ 6/97.)
  //-----------------------------------------------------------------------

  // Initialise the electric field and potential.
  ex = ey = volt = 0.;

  std::complex<double> whelp;

  // Get the mapping of the position.
  std::complex<double> wpos, wdpos;
  ConformalMap(std::complex<double>(xpos, ypos) / m_cotube, wpos, wdpos);
  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    // Compute the contribution to the potential, if needed.
    if (opt) {
      volt -= real(m_sigmat[isw][i]) *
              log(abs((wpos - wmap[i]) / (1. - wpos * conj(wmap[i]))));
    }
    // Compute the contribution to the electric field.
    whelp = wdpos * (1. - pow(abs(wmap[i]), 2)) /
            ((wpos - wmap[i]) * (1. - conj(wmap[i]) * wpos));
    ex += real(m_sigmat[isw][i]) * real(whelp);
    ey -= real(m_sigmat[isw][i]) * imag(whelp);
  }
  ex /= m_cotube;
  ey /= m_cotube;
}

void ComponentAnalyticField::WfieldPlaneA00(const double xpos,
                                            const double ypos, double& ex,
                                            double& ey, double& volt,
                                            const int mx, const int my,
                                            const int iplane, 
                                            const bool opt) const {

  //-----------------------------------------------------------------------
  //   IPLA00 - Routine returning the A I,J [MX,MY] * E terms for A cells.
  //   VARIABLES : R2         : Potential before taking -Log(Sqrt(...))
  //               EX,EY      : x,y-Component of the electric field.
  //               EXHELP ETC : One term in the summing series.
  //               (XPOS,YPOS): Position where the field is needed.
  //   (Last changed on  9/11/98.)
  //-----------------------------------------------------------------------

  // Initialise the electric field and potential.
  ex = ey = volt = 0.;

  double xxmirr = 0., yymirr = 0.;
  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    // Define a few reduced variables.
    const double xx = xpos - m_w[i].x - mx * m_sx;
    const double yy = ypos - m_w[i].y - my * m_sy;
    // Calculate the field in case there are no planes.
    double r2 = xx * xx + yy * yy;
    if (r2 <= 0.) continue;
    double exhelp = xx / r2;
    double eyhelp = yy / r2;
    // Take care of a planes at constant x.
    if (m_ynplax) {
      xxmirr = xpos + m_w[i].x - 2 * m_coplax;
      const double r2plan = xxmirr * xxmirr + yy * yy;
      if (r2plan <= 0.) continue;
      exhelp -= xxmirr / r2plan;
      eyhelp -= yy / r2plan;
      r2 /= r2plan;
    }
    // Take care of a plane at constant y.
    if (m_ynplay) {
      yymirr = ypos + m_w[i].y - 2 * m_coplay;
      const double r2plan = xx * xx + yymirr * yymirr;
      if (r2plan <= 0.) continue;
      exhelp -= xx / r2plan;
      eyhelp -= yymirr / r2plan;
      r2 /= r2plan;
    }
    // Take care of pairs of planes.
    if (m_ynplax && m_ynplay) {
      const double r2plan = xxmirr * xxmirr + yymirr * yymirr;
      if (r2plan <= 0.) continue;
      exhelp += xxmirr / r2plan;
      eyhelp += yymirr / r2plan;
      r2 *= r2plan;
    }
    // Calculate the electric field and potential.
    if (opt) volt -= 0.5 * m_qplane[iplane][i] * log(r2);
    ex += m_qplane[iplane][i] * exhelp;
    ey += m_qplane[iplane][i] * eyhelp;
  }
}

void ComponentAnalyticField::WfieldPlaneB2X(const double xpos,
                                            const double ypos, double& ex,
                                            double& ey, double& volt,
                                            const int my, const int iplane,
                                            const bool opt) const {

  //-----------------------------------------------------------------------
  //   IPLB2X - Routine calculating the MY contribution to the signal on
  //            wire IPLANE due to a charge at (XPOS,YPOS) for F-B2Y cells.
  //   VARIABLES : See routine EFCA00 for most of the variables.
  //               Z,ZZMIRR   : X + I*Y , XXMIRR + I*YYMIRR ; I**2=-1
  //               ECOMPL     : EX + I*EY                   ; I**2=-1
  //   (Last changed on 12/11/98.)
  //-----------------------------------------------------------------------

  std::complex<double> zz, ecompl, zzmirr, zzneg, zznmirr;

  // Initialise ex, ey and volt.
  ex = ey = volt = 0.;
  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    const double xx = HalfPi * (xpos - m_w[i].x) / m_sx;
    const double yy = HalfPi * (ypos - m_w[i].y - my * m_sy) / m_sx;
    const double xxneg = HalfPi * (xpos + m_w[i].x - 2 * m_coplan[0]) / m_sx;
    zz = std::complex<double>(xx, yy);
    zzneg = std::complex<double>(xxneg, yy);
    // Calculate the field in case there are no equipotential planes.
    ecompl = 0.;
    double r2 = 1.;
    if (fabs(yy) <= 20.) {
      ecompl = -b2sin[i] / (sin(zz) * sin(zzneg));
      if (opt) {
        r2 = (pow(sinh(yy), 2) + pow(sin(xx), 2)) /
             (pow(sinh(yy), 2) + pow(sin(xxneg), 2));
      }
    }
    // Take care of a plane at constant y.
    if (m_ynplay) {
      const double yymirr = (HalfPi / m_sx) * (ypos + m_w[i].y - 2 * m_coplay);
      zzmirr = std::complex<double>(yy, yymirr);
      zznmirr = std::complex<double>(xxneg, yymirr);
      if (fabs(yymirr) <= 20.) {
        ecompl += b2sin[i] / (sin(zzmirr) * sin(zznmirr));
        if (opt) {
          const double r2plan = (pow(sinh(yymirr), 2) + pow(sin(xx), 2)) /
                                (pow(sinh(yymirr), 2) + pow(sin(xxneg), 2));
          r2 /= r2plan;
        }
      }
    }
    // Calculate the electric field.
    ex += m_qplane[iplane][i] * real(ecompl);
    ey -= m_qplane[iplane][i] * imag(ecompl);
    if (opt) volt -= 0.5 * m_qplane[iplane][i] * log(r2);
  }
  ex *= (HalfPi / m_sx);
  ey *= (HalfPi / m_sx);
}

void ComponentAnalyticField::WfieldPlaneB2Y(const double xpos,
                                            const double ypos, double& ex,
                                            double& ey, double& volt,
                                            const int mx, const int iplane,
                                            const bool opt) const {

  //-----------------------------------------------------------------------
  //   IPLB2Y - Routine calculating the MX contribution to the signal on
  //            wire IPLANE due to a charge at (XPOS,YPOS) for F-B2X cells.
  //   VARIABLES : See routine EFCA00 for most of the variables.
  //               Z,ZZMIRR   : X + I*Y , XXMIRR + I*YYMIRR ; I**2=-1
  //               ECOMPL     : EX + I*EY                   ; I**2=-1
  //   (Last changed on 12/11/98.)
  //-----------------------------------------------------------------------

  const std::complex<double> icons = std::complex<double>(0., 1.);

  std::complex<double> zz, ecompl, zzmirr, zzneg, zznmirr;

  // Initialise ex, ey and volt.
  ex = ey = volt = 0.;
  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    const double xx = HalfPi * (xpos - m_w[i].x - mx * m_sx) / m_sy;
    const double yy = HalfPi * (ypos - m_w[i].y) / m_sy;
    const double yyneg = HalfPi * (ypos + m_w[i].y - 2 * m_coplan[2]) / m_sy;
    zz = std::complex<double>(xx, yy);
    zzneg = std::complex<double>(xx, yyneg);
    // Calculate the field in case there are no equipotential planes.
    ecompl = 0.;
    double r2 = 1.;
    if (fabs(xx) <= 20.) {
      ecompl = icons * b2sin[i] / (sin(icons * zz) * sin(icons * zzneg));
      if (opt) {
        r2 = (pow(sinh(xx), 2) + pow(sin(yy), 2)) /
             (pow(sinh(xx), 2) + pow(sin(yyneg), 2));
      }
    }
    // Take care of a plane at constant y.
    if (m_ynplax) {
      const double xxmirr = (HalfPi / m_sy) * (xpos + m_w[i].x - 2 * m_coplax);
      zzmirr = std::complex<double>(xxmirr, yy);
      zznmirr = std::complex<double>(xxmirr, yyneg);
      if (fabs(xxmirr) <= 20.) {
        ecompl -= b2sin[i] / (sin(icons * zzmirr) * sin(icons * zznmirr));
        if (opt) {
          const double r2plan = (pow(sinh(xxmirr), 2) + pow(sin(yy), 2)) /
                                (pow(sinh(xxmirr), 2) + pow(sin(yyneg), 2));
          r2 /= r2plan;
        }
      }
    }
    // Calculate the electric field and potential.
    ex += m_qplane[iplane][i] * real(ecompl);
    ey -= m_qplane[iplane][i] * imag(ecompl);
    if (opt) volt -= 0.5 * m_qplane[iplane][i] * log(r2);
  }
  ex *= HalfPi / m_sy;
  ey *= HalfPi / m_sy;
}

void ComponentAnalyticField::WfieldPlaneC2X(const double xpos,
                                            const double ypos, double& ex,
                                            double& ey, double& volt,
                                            const int iplane, 
                                            const bool opt) const {

  //-----------------------------------------------------------------------
  //   IPLC2X - Routine returning the potential and electric field in a
  //            configuration with 2 x planes and y periodicity.
  //   VARIABLES : see the writeup
  //   (Last changed on 12/10/06.)
  //-----------------------------------------------------------------------

  const std::complex<double> icons = std::complex<double>(0., 1.);

  std::complex<double> zsin, zcof, zu, zunew, zterm1, zterm2, zeta;
  // Initial values.
  std::complex<double> wsum1 = 0.;
  std::complex<double> wsum2 = 0.;
  double s = 0.;
  volt = 0.;

  // Wire loop.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Compute the direct contribution.
    zeta = m_zmult * std::complex<double>(xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > +15.) {
      wsum1 -= m_qplane[iplane][i] * icons;
      if (opt) volt -= m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum1 += m_qplane[iplane][i] * icons;
      if (opt) volt -= m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum1 += m_qplane[iplane][i] * (zterm2 / zterm1);
      if (opt) volt -= m_qplane[iplane][i] * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    double cx = m_coplax - m_sx * int(round((m_coplax - m_w[i].x) / m_sx));
    // Constant terms sum
    s += m_qplane[iplane][i] * (m_w[i].x - cx);
    // Mirror contribution.
    zeta = m_zmult *
           std::complex<double>(2. * cx - xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum2 -= m_qplane[iplane][i] * icons;
      if (opt) volt += m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum2 += m_qplane[iplane][i] * icons;
      if (opt) volt += m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum2 += m_qplane[iplane][i] * (zterm2 / zterm1);
      if (opt) volt += m_qplane[iplane][i] * log(abs(zterm1));
    }
    if (opt && mode == 0) {
      volt -= TwoPi * m_qplane[iplane][i] * (xpos - cx) * (m_w[i].x - cx) /
              (m_sx * m_sy);
    }
  }
  // Convert the two contributions to a real field.
  ex = real(m_zmult * (wsum1 + wsum2));
  ey = -imag(m_zmult * (wsum1 - wsum2));
  // Constant correction terms.
  if (mode == 0) ex += s * TwoPi / (m_sx * m_sy);
}

void ComponentAnalyticField::WfieldPlaneC2Y(const double xpos,
                                            const double ypos, double& ex,
                                            double& ey, double& volt,
                                            const int iplane, 
                                            const bool opt) const {

  //-----------------------------------------------------------------------
  //   IPLC2Y - Routine returning the potential and electric field in a
  //            configuration with 2 y planes and x periodicity.
  //   VARIABLES : see the writeup
  //   (Last changed on 12/10/06.)
  //-----------------------------------------------------------------------

  const std::complex<double> icons = std::complex<double>(0., 1.);

  std::complex<double> zsin, zcof, zu, zunew, zterm1, zterm2, zeta;
  // Initial values.
  std::complex<double> wsum1 = 0.;
  std::complex<double> wsum2 = 0.;
  double s = 0.;
  volt = 0.;

  // Wire loop.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Compute the direct contribution.
    zeta = m_zmult * std::complex<double>(xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > +15.) {
      wsum1 -= m_qplane[iplane][i] * icons;
      if (opt) volt -= m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum1 += m_qplane[iplane][i] * icons;
      if (opt) volt -= m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum1 += m_qplane[iplane][i] * (zterm2 / zterm1);
      if (opt) volt -= m_qplane[iplane][i] * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    double cy = m_coplay - m_sy * int(round((m_coplay - m_w[i].y) / m_sy));
    // Constant terms sum
    s += m_qplane[iplane][i] * (m_w[i].y - cy);
    // Mirror contribution.
    zeta = m_zmult *
           std::complex<double>(xpos - m_w[i].x, 2. * cy - ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum2 -= m_qplane[iplane][i] * icons;
      if (opt) volt += m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum2 += m_qplane[iplane][i] * icons;
      if (opt) volt += m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum2 += m_qplane[iplane][i] * (zterm2 / zterm1);
      if (opt) volt += m_qplane[iplane][i] * log(abs(zterm1));
    }
    // Correct the voltage, if needed (MODE).
    if (opt && mode == 1) {
      volt -= TwoPi * m_qplane[iplane][i] * (ypos - cy) * (m_w[i].y - cy) /
              (m_sx * m_sy);
    }
  }
  // Convert the two contributions to a real field.
  ex = real(m_zmult * (wsum1 - wsum2));
  ey = -imag(m_zmult * (wsum1 + wsum2));
  // Constant correction terms.
  if (mode == 1) ey += s * TwoPi / (m_sx * m_sy);
}

void ComponentAnalyticField::WfieldPlaneC30(const double xpos,
                                            const double ypos, double& ex,
                                            double& ey, double& volt,
                                            const int iplane, 
                                            const bool opt) const {

  //-----------------------------------------------------------------------
  //   IPLC30 - Routine returning the weighting field field in a
  //            configuration with 2 y and 2 x planes. This routine is
  //            basically the same as EFCC30.
  //   (Last changed on  9/11/98.)
  //-----------------------------------------------------------------------

  const std::complex<double> icons = std::complex<double>(0., 1.);

  std::complex<double> zsin, zcof, zu, zunew, zterm1, zterm2, zeta;
  // Initial values.
  std::complex<double> wsum1 = 0.;
  std::complex<double> wsum2 = 0.;
  std::complex<double> wsum3 = 0.;
  std::complex<double> wsum4 = 0.;
  volt = 0.;

  // Wire loop.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Compute the direct contribution.
    zeta = m_zmult * std::complex<double>(xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > +15.) {
      wsum1 -= m_qplane[iplane][i] * icons;
      if (opt) volt -= m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum1 += m_qplane[iplane][i] * icons;
      if (opt) volt -= m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum1 += m_qplane[iplane][i] * zterm2 / zterm1;
      if (opt) volt -= m_qplane[iplane][i] * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    double cx = m_coplax - m_sx * int(round((m_coplax - m_w[i].x) / m_sx));
    // Mirror contribution from the x plane.
    zeta = m_zmult *
           std::complex<double>(2. * cx - xpos - m_w[i].x, ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum2 -= m_qplane[iplane][i] * icons;
      if (opt) volt += m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum2 += m_qplane[iplane][i] * icons;
      if (opt) volt += m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum2 += m_qplane[iplane][i] * zterm2 / zterm1;
      if (opt) volt += m_qplane[iplane][i] * log(abs(zterm1));
    }
    // Find the plane nearest to the wire.
    double cy = m_coplay - m_sy * int(round((m_coplay - m_w[i].y) / m_sy));
    // Mirror contribution from the y plane.
    zeta = m_zmult *
           std::complex<double>(xpos - m_w[i].x, 2. * cy - ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum3 -= m_qplane[iplane][i] * icons;
      if (opt) volt += m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum3 += m_qplane[iplane][i] * icons;
      if (opt) volt += m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum3 += m_qplane[iplane][i] * zterm2 / zterm1;
      if (opt) volt += m_qplane[iplane][i] * log(abs(zterm1));
    }
    // Mirror contribution from both the x and the y plane.
    zeta = m_zmult * std::complex<double>(2. * cx - xpos - m_w[i].x,
                                          2. * cy - ypos - m_w[i].y);
    if (imag(zeta) > 15.) {
      wsum4 -= m_qplane[iplane][i] * icons;
      if (opt) volt -= m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else if (imag(zeta) < -15.) {
      wsum4 += m_qplane[iplane][i] * icons;
      if (opt) volt -= m_qplane[iplane][i] * (fabs(imag(zeta)) - CLog2);
    } else {
      zsin = sin(zeta);
      zcof = 4. * zsin * zsin - 2.;
      zu = -m_p1 - zcof * m_p2;
      zunew = 1. - zcof * zu - m_p2;
      zterm1 = (zunew + zu) * zsin;
      zu = -3. * m_p1 - zcof * 5. * m_p2;
      zunew = 1. - zcof * zu - 5. * m_p2;
      zterm2 = (zunew - zu) * cos(zeta);
      wsum4 += m_qplane[iplane][i] * zterm2 / zterm1;
      if (opt) volt -= m_qplane[iplane][i] * log(abs(zterm1));
    }
  }
  ex = real(m_zmult * (wsum1 + wsum2 - wsum3 - wsum4));
  ey = -imag(m_zmult * (wsum1 - wsum2 + wsum3 - wsum4));
}

void ComponentAnalyticField::WfieldPlaneD10(const double xpos,
                                            const double ypos, double& ex,
                                            double& ey, double& volt,
                                            const int iplane, 
                                            const bool opt) const {

  //-----------------------------------------------------------------------
  //   IPLD10 - Subroutine computing the signal on wire IPLANE due to a
  //            charge at (XPOS,YPOS). This is effectively routine EFCD10.
  //   VARIABLES : EX, EY     : Electric field.
  //               (XPOS,YPOS): The position where the field is calculated.
  //               ZI, ZPOS   : Shorthand complex notations.
  //   (Last changed on  9/11/98.)
  //-----------------------------------------------------------------------

  // Initialise the electric field and potential.
  ex = ey = volt = 0.;

  // Set the complex position coordinates.
  std::complex<double> zpos = std::complex<double>(xpos, ypos);
  std::complex<double> zi;
  std::complex<double> wi;
  const double r2 = m_cotube * m_cotube;
  // Loop over all wires.
  for (int i = m_nWires; i--;) {
    // Set the complex version of the wire-coordinate for simplicity.
    zi = std::complex<double>(m_w[i].x, m_w[i].y);
    // Compute the contribution to the potential, if needed.
    if (opt) {
      volt -= m_qplane[iplane][i] *
              log(abs(m_cotube * (zpos - zi) / (r2 - zpos * conj(zi))));
    }
    // Compute the contribution to the electric field.
    wi = 1. / conj(zpos - zi) + zi / (r2 - conj(zpos) * zi);
    ex += m_qplane[iplane][i] * real(wi);
    ey += m_qplane[iplane][i] * imag(wi);
  }
}

void ComponentAnalyticField::WfieldPlaneD30(const double xpos,
                                            const double ypos, double& ex,
                                            double& ey, double& volt,
                                            const int iplane, 
                                            const bool opt) const {

  //-----------------------------------------------------------------------
  //   IPLD30 - Subroutine computing the weighting field for a polygonal
  //            cells without periodicities, type D3.
  //   VARIABLES : EX, EY     : Electric field
  //               (XPOS,YPOS): The position where the field is calculated.
  //               ZI, ZPOS   : Shorthand complex notations.
  //   (Last changed on  9/11/98.)
  //-----------------------------------------------------------------------

  // Initialise the weighting field and potential.
  ex = ey = volt = 0.;

  std::complex<double> whelp;

  // Get the mapping of the position.
  std::complex<double> wpos, wdpos;
  ConformalMap(std::complex<double>(xpos, ypos) / m_cotube, wpos, wdpos);
  // Loop over all wires.
  for (unsigned int i = 0; i < m_nWires; ++i) {
    // Compute the contribution to the potential, if needed.
    if (opt) {
      volt -= m_qplane[iplane][i] *
              log(abs((wpos - wmap[i]) / (1. - wpos * conj(wmap[i]))));
    }
    // Compute the contribution to the electric field.
    whelp = wdpos * (1. - pow(abs(wmap[i]), 2)) /
            ((wpos - wmap[i]) * (1. - conj(wmap[i]) * wpos));
    ex += m_qplane[iplane][i] * real(whelp);
    ey -= m_qplane[iplane][i] * imag(whelp);
  }
  ex /= m_cotube;
  ey /= m_cotube;
}

void ComponentAnalyticField::WfieldStripZ(const double xpos, const double ypos,
                                          double& ex, double& ey, double& volt,
                                          const int ip, const int is,
                                          const bool opt) const {

  //-----------------------------------------------------------------------
  //   IONEST - Weighting field for strips.
  //   (Last changed on  6/12/00.)
  //-----------------------------------------------------------------------

  // Initialise the weighting field and potential.
  ex = ey = volt = 0.;

  strip theStrip = planes[ip].strips2[is];
  // Transform to normalised coordinates.
  double xw = 0., yw = 0.;
  switch (ip) {
    case 0:
      xw = -ypos + (theStrip.smin + theStrip.smax) / 2.;
      yw = xpos - m_coplan[ip];
      break;
    case 1:
      xw = ypos - (theStrip.smin + theStrip.smax) / 2.;
      yw = m_coplan[ip] - xpos;
      break;
    case 2:
      xw = xpos - (theStrip.smin + theStrip.smax) / 2.;
      yw = ypos - m_coplan[ip];
      break;
    case 3:
      xw = -xpos + (theStrip.smin + theStrip.smax) / 2.;
      yw = m_coplan[ip] - ypos;
      break;
    default:
      return;
  }
  // Store the gap and strip halfwidth.
  const double w = fabs(theStrip.smax - theStrip.smin) / 2.;
  const double g = theStrip.gap;

  // Make sure we are in the fiducial part of the weighting map.
  if (yw <= 0. || yw > g) return;

  // Define shorthand notations.
  const double s = sin(Pi * yw / g);
  const double c = cos(Pi * yw / g);
  const double e1 = exp(Pi * (w - xw) / g);
  const double e2 = exp(-Pi * (w + xw) / g);
  const double ce12 = pow(c - e1, 2);
  const double ce22 = pow(c - e2, 2);
  // Check for singularities.
  if (c == e1 || c == e2) return;
  // Evaluate the potential, if requested.
  if (opt) {
    volt = atan((c - e2) / s) - atan((c - e1) / s);
    volt /= Pi;
  }
  // Evaluate the field.
  const double ewx = (s / g) * (e1 / (ce12 + s * s) - e2 / (ce22 + s * s));
  const double ewy = ((c / (c - e2) + s * s / ce22) / (1. + s * s / ce22) -
                      (c / (c - e1) + s * s / ce12) / (1. + s * s / ce12)) /
                     g;

  // Rotate the field back to the original coordinates.
  switch (ip) {
    case 0:
      ex = ewy;
      ey = -ewx;
      break;
    case 1:
      ex = -ewy;
      ey = ewx;
      break;
    case 2:
      ex = ewx;
      ey = ewy;
      break;
    case 3:
      ex = -ewx;
      ey = -ewy;
      break;
  }
}

void ComponentAnalyticField::WfieldStripXy(const double xpos, const double ypos,
                                           const double zpos, double& ex,
                                           double& ey, double& ez, double& volt,
                                           const int ip, const int is,
                                           const bool opt) const {

  //-----------------------------------------------------------------------
  //   IONEST - Weighting field for strips.
  //   (Last changed on  6/12/00.)
  //-----------------------------------------------------------------------

  // Initialise the weighting field and potential.
  ex = ey = ez = volt = 0.;

  strip theStrip = planes[ip].strips1[is];
  // Transform to normalised coordinates.
  double xw = 0., yw = 0.;
  switch (ip) {
    case 0:
      xw = -zpos + (theStrip.smin + theStrip.smax) / 2.;
      yw = xpos - m_coplan[ip];
      break;
    case 1:
      xw = zpos - (theStrip.smin + theStrip.smax) / 2.;
      yw = m_coplan[ip] - xpos;
      break;
    case 2:
      xw = zpos - (theStrip.smin + theStrip.smax) / 2.;
      yw = ypos - m_coplan[ip];
      break;
    case 3:
      xw = -zpos + (theStrip.smin + theStrip.smax) / 2.;
      yw = m_coplan[ip] - ypos;
      break;
    default:
      return;
  }

  // Store the gap and strip halfwidth.
  const double w = fabs(theStrip.smax - theStrip.smin) / 2.;
  const double g = theStrip.gap;

  // Make sure we are in the fiducial part of the weighting map.
  if (yw <= 0. || yw > g) return;

  // Define shorthand notations.
  const double s = sin(Pi * yw / g);
  const double c = cos(Pi * yw / g);
  const double e1 = exp(Pi * (w - xw) / g);
  const double e2 = exp(-Pi * (w + xw) / g);
  const double ce12 = pow(c - e1, 2);
  const double ce22 = pow(c - e2, 2);
  // Check for singularities.
  if (c == e1 || c == e2) return;
  // Evaluate the potential, if requested.
  if (opt) {
    volt = atan((c - e2) / s) - atan((c - e1) / s);
    volt /= Pi;
  }
  // Evaluate the field.
  const double ewx = (s / g) * (e1 / (ce12 + s * s) - e2 / (ce22 + s * s));
  const double ewy = ((c / (c - e2) + s * s / ce22) / (1. + s * s / ce22) -
                      (c / (c - e1) + s * s / ce12) / (1. + s * s / ce12)) / g;

  // Rotate the field back to the original coordinates.
  switch (ip) {
    case 0:
      ex = ewy;
      ey = 0.;
      ez = -ewx;
      break;
    case 1:
      ex = -ewy;
      ey = 0.;
      ez = ewx;
      break;
    case 2:
      ex = 0.;
      ey = ewy;
      ez = ewx;
      break;
    case 3:
      ex = 0.;
      ey = -ewy;
      ez = -ewx;
      break;
  }
}

void ComponentAnalyticField::WfieldPixel(const double xpos, const double ypos,
                                         const double zpos, double& ex,
                                         double& ey, double& ez, double& volt,
                                         const int ip, const int is,
                                         const bool opt) const {
  //-----------------------------------------------------------------------
  //   Weighting field for pixels.
  //-----------------------------------------------------------------------
  // W. Riegler, G. Aglieri Rinella, 
  // Point charge potential and weighting field of a pixel or pad 
  // in a plane condenser,
  // Nucl. Instr. Meth. A 767, 2014, 267 - 270
  // http://dx.doi.org/10.1016/j.nima.2014.08.044 

  // Initialise the weighting field and potential.
  ex = ey = ez = volt = 0.;

  const pixel& thePixel = planes[ip].pixels[is];
  const double d = thePixel.gap;
  // Transform to standard coordinates.
  double x = 0., y = 0., z = 0.;

  // Pixel centre and widths.
  const double ps = 0.5 * (thePixel.smin + thePixel.smax);
  const double pz = 0.5 * (thePixel.zmin + thePixel.zmax);
  const double wx = thePixel.smax - thePixel.smin;
  const double wy = thePixel.zmax - thePixel.zmin;
  switch (ip) {
    case 0:
      x = ypos - ps;
      y = zpos - pz;
      z = xpos - m_coplan[ip];
      break;
    case 1:
      x =  ypos - ps;
      y = -zpos + pz;
      z = -xpos + m_coplan[ip];
      break;
    case 2:
      x =  xpos - ps;
      y = -zpos + pz;
      z =  ypos - m_coplan[ip];
      break;
    case 3:
      x =  xpos - ps;
      y =  zpos - pz;
      z = -ypos + m_coplan[ip];
      break;
    default:
      return;
  }
  if (z < 0.) std::cerr << " z = " << z << std::endl;
  // Make sure we are in the fiducial part of the weighting map.
  // Commenting out this lines either breaks the simulation or the plot!
  // TODO!
  // if (z <= 0. || z > d) return;

  // Define shorthand notations and common terms.
  const double x1 = x - wx / 2.;
  const double x2 = x + wx / 2.;
  const double y1 = y - wy / 2.;
  const double y2 = y + wy / 2.;
  const double x1s = x1 * x1;
  const double x2s = x2 * x2;
  const double y1s = y1 * y1;
  const double y2s = y2 * y2;

  // Calculate number of terms needed to have sufficiently small error.
  const double maxError = 1.e-5;
  const double d3 = d * d * d;
  const unsigned int nz =
      std::ceil(sqrt(wx * wy / (8 * Pi * d3 * maxError)));
  const unsigned int nx =
      std::ceil(sqrt(wy * z / (4 * Pi * d3 * maxError)));
  const unsigned int ny =
      std::ceil(sqrt(wx * z / (4 * Pi * d3 * maxError)));
  const unsigned int nn = std::max(ny, std::max(nx, nz));
  for (unsigned int i = 1; i <= nn; ++i) {
    const double u1 = 2 * i * d - z;
    const double u2 = 2 * i * d + z;
    const double u1s = u1 * u1;
    const double u2s = u2 * u2;
    const double u1x1y1 = sqrt(x1s + y1s + u1s);
    const double u1x1y2 = sqrt(x1s + y2s + u1s);
    const double u1x2y1 = sqrt(x2s + y1s + u1s);
    const double u1x2y2 = sqrt(x2s + y2s + u1s);
    const double u2x1y1 = sqrt(x1s + y1s + u2s);
    const double u2x1y2 = sqrt(x1s + y2s + u2s);
    const double u2x2y1 = sqrt(x2s + y1s + u2s);
    const double u2x2y2 = sqrt(x2s + y2s + u2s);

    if (i <= nx) {
      //-df/dx(x,y,2nd-z)
      ex -=
           u1 * y1 / ((u1s + x2s) * u1x2y1) - u1 * y1 / ((u1s + x1s) * u1x1y1) +
           u1 * y2 / ((u1s + x1s) * u1x1y2) - u1 * y2 / ((u1s + x2s) * u1x2y2);

      //-df/dx(x,y,2nd+z)
      ex +=
           u2 * y1 / ((u2s + x2s) * u2x2y1) - u2 * y1 / ((u2s + x1s) * u2x1y1) +
           u2 * y2 / ((u2s + x1s) * u2x1y2) - u2 * y2 / ((u2s + x2s) * u2x2y2);
    }
    if (i <= ny) {
      //-df/dy(x,y,2nd-z)
      ey -=
           u1 * x1 / ((u1s + y2s) * u1x1y2) - u1 * x1 / ((u1s + y1s) * u1x1y1) +
           u1 * x2 / ((u1s + y1s) * u1x2y1) - u1 * x2 / ((u1s + y2s) * u1x2y2);

      //-df/dy(x,y,2nd+z)
      ey +=
           u2 * x1 / ((u2s + y2s) * u2x1y2) - u2 * x1 / ((u2s + y1s) * u2x1y1) +
           u2 * x2 / ((u2s + y1s) * u2x2y1) - u2 * x2 / ((u2s + y2s) * u2x2y2);
    }
    if (i <= nz) {
      //-df/dz(x,y,2nd-z)
      ez += x1 * y1 * (x1s + y1s + 2 * u1s) /
                ((x1s + u1s) * (y1s + u1s) * u1x1y1) +
            x2 * y2 * (x2s + y2s + 2 * u1s) /
                ((x2s + u1s) * (y2s + u1s) * u1x2y2) -
            x1 * y2 * (x1s + y2s + 2 * u1s) /
                ((x1s + u1s) * (y2s + u1s) * u1x1y2) -
            x2 * y1 * (x2s + y1s + 2 * u1s) /
                ((x2s + u1s) * (y1s + u1s) * u1x2y1);

      //-df/dz(x,y,2nd+z)
      ez += x1 * y1 * (x1s + y1s + 2 * u2s) /
                ((x1s + u2s) * (y1s + u2s) * u2x1y1) +
            x2 * y2 * (x2s + y2s + 2 * u2s) /
                ((x2s + u2s) * (y2s + u2s) * u2x2y2) -
            x1 * y2 * (x1s + y2s + 2 * u2s) /
                ((x1s + u2s) * (y2s + u2s) * u2x1y2) -
            x2 * y1 * (x2s + y1s + 2 * u2s) /
                ((x2s + u2s) * (y1s + u2s) * u2x2y1);
    }
    if (!opt) continue;
    volt -= atan(x1 * y1 / (u1 * u1x1y1)) + atan(x2 * y2 / (u1 * u1x2y2)) -
            atan(x1 * y2 / (u1 * u1x1y2)) - atan(x2 * y1 / (u1 * u1x2y1)); 
    volt += atan(x1 * y1 / (u2 * u2x1y1)) + atan(x2 * y2 / (u2 * u2x2y2)) -
            atan(x1 * y2 / (u2 * u2x1y2)) - atan(x2 * y1 / (u2 * u2x2y1)); 
  }

  const double zs = z * z;
  const double x1y1 = sqrt(x1s + y1s + zs);
  const double x1y2 = sqrt(x1s + y2s + zs);
  const double x2y1 = sqrt(x2s + y1s + zs);
  const double x2y2 = sqrt(x2s + y2s + zs);
  //-df/dx(x,y,z)
  ex += z * y1 / ((zs + x2s) * x2y1) - z * y1 / ((zs + x1s) * x1y1) +
        z * y2 / ((zs + x1s) * x1y2) - z * y2 / ((zs + x2s) * x2y2);

  //-df/y(x,y,z)
  ey += z * x1 / ((zs + y2s) * x1y2) - z * x1 / ((zs + y1s) * x1y1) +
        z * x2 / ((zs + y1s) * x2y1) - z * x2 / ((zs + y2s) * x2y2);

  //-df/dz(x,y,z)
  ez += x1 * y1 * (x1s + y1s + 2 * zs) / ((x1s + zs) * (y1s + zs) * x1y1) +
        x2 * y2 * (x2s + y2s + 2 * zs) / ((x2s + zs) * (y2s + zs) * x2y2) -
        x1 * y2 * (x1s + y2s + 2 * zs) / ((x1s + zs) * (y2s + zs) * x1y2) -
        x2 * y1 * (x2s + y1s + 2 * zs) / ((x2s + zs) * (y1s + zs) * x2y1);

  ex /= TwoPi;
  ey /= TwoPi;
  ez /= TwoPi;

  if (opt) {
    volt += atan(x1 * y1 / (z * x1y1)) + atan(x2 * y2 / (z * x2y2)) -
            atan(x1 * y2 / (z * x1y2)) - atan(x2 * y1 / (z * x2y1)); 
    volt /= TwoPi;
  }

  // Rotate the field back to the original coordinates.
  const double fx = ex;
  const double fy = ey;
  const double fz = ez;
  switch (ip) {
    case 0:
      ex = fz;
      ey = fx;
      ez = fy;
      break;
    case 1:
      ex = -fz;
      ey =  fx;
      ez = -fy;
      break;
    case 2:
      ex =  fx;
      ey =  fz;
      ez = -fy;
      break;
    case 3:
      ex =  fx;
      ey = -fz;
      ez =  fy;
      break;
  }
}
}
