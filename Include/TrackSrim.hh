#ifndef G_TRACK_SRIM_H
#define G_TRACK_SRIM_H
#include <vector>
#include "Track.hh"

namespace Garfield {

class TrackSrim : public Track {

 public:
  /// Constructor
  TrackSrim();
  /// Destructor
  ~TrackSrim() {}

  /// Set/get the W value [eV].
  void SetWorkFunction(const double w) { m_work = w; }
  double GetWorkFunction() const { return m_work; }
  /// Set/get the Fano factor.
  void SetFanoFactor(const double f) { m_fano = f; }
  double GetFanoFactor() const { return m_fano; }
  /// Set/get the density [g/cm3] of the target medium.
  void SetDensity(const double density) { m_density = density; }
  double GetDensity() const { return m_density; }
  /// Set/get A and Z of the target medium.
  void SetAtomicMassNumbers(const double a, const double z) {
    m_a = a;
    m_z = z;
  }
  void GetAtomicMassMumbers(double& a, double& z) const {
    a = m_a;
    z = m_z;
  }

  /// Set/get the fluctuation model 
  /// (0 = none, 1 = Landau, 2 = Vavilov, 3 = Gaussian, 4 = Combined)
  void SetModel(const int m) { m_model = m; }
  int GetModel() const { return m_model; }

  void EnableTransverseStraggling() { m_useTransStraggle = true; }
  void DisableTransverseStraggling() { m_useTransStraggle = false; }
  void EnableLongitudinalStraggling() { m_useLongStraggle = true; }
  void DisableLongitudinalStraggling() { m_useLongStraggle = false; }

  void EnablePreciseVavilov() { m_precisevavilov = true; }
  void DisablePreciseVavilov() { m_precisevavilov = false; }

  void SetTargetClusterSize(const int n) { m_nsize = n; }
  int GetTargetClusterSize() const { return m_nsize; }

  void SetClustersMaximum(const int n) { m_maxclusters = n; }
  int SetClustersMaximum() const { return m_maxclusters; }

  bool ReadFile(const std::string& file);
  void Print();
  void PlotEnergyLoss();
  void PlotRange();
  void PlotStraggling();

  bool NewTrack(const double x0, const double y0, const double z0,
                const double t0, const double dx0, const double dy0,
                const double dz0);
  bool GetCluster(double& xcls, double& ycls, double& zcls, double& tcls,
                  int& n, double& e, double& extra);

 protected:
  /// Use precise Vavilov generator
  bool m_precisevavilov;
  /// Include transverse straggling
  bool m_useTransStraggle;
  /// Include longitudinal straggling
  bool m_useLongStraggle;

  /// Charge gas been defined
  bool m_chargeset;
  /// Density [g/cm3]
  double m_density;
  /// Work function [eV]
  double m_work;
  /// Fano factor [-]
  double m_fano;
  /// Charge of ion
  double m_q;
  /// Mass of ion [MeV]
  double m_mass;
  /// A and Z of the gas
  double m_a;
  double m_z;

  /// Maximum number of clusters allowed (infinite if 0)
  int m_maxclusters;
  /// Energy in energy loss table [MeV]
  std::vector<double> m_ekin;
  /// EM energy loss [MeV cm2/g]
  std::vector<double> m_emloss;
  /// Hadronic energy loss [MeV cm2/g]
  std::vector<double> m_hdloss;
  /// Projected range [cm]
  std::vector<double> m_range;
  /// Transverse straggling [cm]
  std::vector<double> m_transstraggle;
  /// Longitudinal straggling [cm]
  std::vector<double> m_longstraggle;

  /// Index of the next cluster to be returned
  unsigned int m_currcluster;
  /// Fluctuation model (0 = none, 1 = Landau, 2 = Vavilov,
  ///                    3 = Gaussian, 4 = Combined)
  unsigned int m_model;
  /// Targeted cluster size
  int m_nsize;
  struct cluster {
    double x, y, z, t; // Cluster location and time
    double ec;         // Energy spent to make the clusterec
    double kinetic;    // Ion energy when cluster was created
    int electrons;     // Number of electrons in this cluster
  };
  std::vector<cluster> m_clusters;

  double DedxEM(const double e) const;
  double DedxHD(const double e) const;
  bool PreciseLoss(const double step, const double estart, double& deem,
                   double& dehd) const;
  bool EstimateRange(const double ekin, const double step, double& stpmax);
  bool SmallestStep(double ekin, double de, double step, double& stpmin);

  double RndmEnergyLoss(const double ekin, const double de, 
                        const double step) const;
};
}

#endif
