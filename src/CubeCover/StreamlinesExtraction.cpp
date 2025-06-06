#include "StreamlinesExtraction.h"

#include <Eigen/Core>
#include <Eigen/Dense>
#include <array>
#include <cassert>
#include <iostream>
#include <random>
#include <unordered_set>
#include <vector>

namespace CubeCover {
// check whether a line: l(t) = p0 + t * dir is intersection with the triangle v0, v1, v2;
bool IntersectTriangle(const Eigen::Vector3d& v0, const Eigen::Vector3d& v1, const Eigen::Vector3d& v2,
                       const Eigen::Vector3d& p0, const Eigen::Vector3d& dir, double& t, bool is_debug) {
  Eigen::Vector3d n = (v1 - v0).cross(v2 - v0);
  n.normalize();

  // parallel case
  if (std::abs(n.dot(dir)) < 1e-6) {
    if (is_debug) {
      std::cout << "parallel case: " << std::endl;
      std::cout << "normal: " << n.transpose() << std::endl;
      std::cout << "dir: " << dir.transpose() << std::endl;
    }
    return false;
  }

  Eigen::Matrix3d A;
  A.col(0) = v1 - v0;
  A.col(1) = v2 - v0;
  A.col(2) = -dir;

  Eigen::Vector3d rhs = p0 - v0;

  if (std::abs(A.determinant()) < 1e-10) {
    return false;
  }
  Eigen::Vector3d sol = A.inverse() * rhs;

  t = sol[2];

  if (is_debug) {
    std::cout << "intersected pt barycentrics: " << sol[0] << ", " << sol[1] << ", " << 1 - sol[0] - sol[1]
              << std::endl;
  }
  return (sol[0] >= 0) && (sol[1] >= 0) && (sol[0] + sol[1]) <= 1;
}

// return a random number between [min, max]
static double RandomInRange(double min, double max) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> dis(0.0, 1.0);
  return min + (max - min) * dis(gen);
}

// sample a point inside the tet (may lay on the boundary): if not random, return the centroid
static Eigen::Vector3d SamplePointInsideTet(const Eigen::MatrixXd& V, const TetMeshConnectivity& mesh, int tet_id,
                                            bool is_random = false) {
  std::array<double, 4> barycentrics = {0.25, 0.25, 0.25, 0.25};
  if (is_random) {
    barycentrics[0] = RandomInRange(0, 1);
    barycentrics[1] = RandomInRange(0, 1 - barycentrics[0]);
    barycentrics[2] = RandomInRange(0, 1 - barycentrics[0] - barycentrics[1]);
    barycentrics[3] = 1 - barycentrics[0] - barycentrics[1] - barycentrics[2];
  }

  Eigen::Vector3d pt = Eigen::Vector3d::Zero();
  for (int i = 0; i < 4; i++) {
    pt += V.row(mesh.tetVertex(tet_id, i)).transpose() * barycentrics[i];
  }
  return pt;
}

// Random Sampling the stream points: sampling "nsamples" tets randomly as the seed of the streamline tracing. If
// is_random_inside = false, we also random sample a point inside the sampled the tet. Otherwise, we use the tet
// centroid
std::vector<std::pair<int, Eigen::Vector3d>> RandomSampleStreamPts(const Eigen::MatrixXd& V,
                                                                   const TetMeshConnectivity& mesh, int nsamples,
                                                                   bool is_random_inside) {
  int ntets = mesh.nTets();
  std::vector<std::pair<int, Eigen::Vector3d>> samples;

  if (ntets <= nsamples) {
    samples.resize(ntets);
    for (int i = 0; i < ntets; i++) {
      samples[i].first = i;
      samples[i].second = SamplePointInsideTet(V, mesh, i, is_random_inside);
    }
    return samples;
  } else {
    std::unordered_set<int> sampled_ids;
    while (sampled_ids.size() < nsamples) {
      int id = std::rand() % ntets;
      if (!sampled_ids.count(id)) {
        sampled_ids.insert(id);
      }
    }
    std::vector<int> tmp_vec;
    tmp_vec.insert(tmp_vec.end(), sampled_ids.begin(), sampled_ids.end());

    for (auto tid : tmp_vec) {
      Eigen::Vector3d pt = SamplePointInsideTet(V, mesh, tid, is_random_inside);
      samples.push_back({tid, pt});
    }
  }
  return samples;
}

// Check whether there is f(x) = iso_val across this segment whose endpoints' values are val1 and val2.
static bool IsCrossing(double val1, double val2, double iso_val, bool flip) {
  if (!flip) {
    return val1 < iso_val && iso_val <= val2;
  } else {
    return val1 <= iso_val && iso_val < val2;
  }
}

std::vector<std::pair<Eigen::Vector3i, Eigen::Vector3d>> ComputeGridPts(const Eigen::MatrixXd& V,
                                                                        const TetMeshConnectivity& mesh,
                                                                        const Eigen::MatrixXd& values, int tet_id,
                                                                        double perturb_eps) {
  if (values.cols() != 3) {
    return {};
  }

  // Assume p = (1 - c0 - c1 - c2) V0 + c0 V1 + c1 V2 + c2 V3, then
  // F[p] = (1 - c0 - c1 - c2) F0 + c0 F1 + c1 F2 + c2 F3 = c0 (F1 - F0) + c1 (F2 - F0) + c2 (F3 - F0) + F0
  // [F1 - F0, F2 - F0 , F3 - F0] * [c0, c1, c2]^T = F[p] - F0

  Eigen::Matrix3d A(3, 3);
  Eigen::Vector3d b(3);
  std::array<double, 3> min_vals = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                                    std::numeric_limits<double>::max()};
  std::array<double, 3> max_vals = {std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
                                    std::numeric_limits<double>::lowest()};

  for (int c = 0; c < 3; c++) {
    A.row(c) << values(4 * tet_id + 1, c) - values(4 * tet_id + 0, c),
        values(4 * tet_id + 2, c) - values(4 * tet_id + 0, c), values(4 * tet_id + 3, c) - values(4 * tet_id + 0, c);
    b(c) = values(4 * tet_id + 0, c);

    for (int j = 0; j < 4; j++) {
      double val = values(4 * tet_id + j, c);
      if (val < min_vals[c]) {
        min_vals[c] = val;
      }
      if (val > max_vals[c]) {
        max_vals[c] = val;
      }
    }
  }

  std::unordered_map<int, int> vert_id_local_tvid;
  bool is_bnd_tet = false;
  std::array<bool, 4> is_bnd_face = {false, false, false, false};
  for (int i = 0; i < 4; i++) {
    int vid = mesh.tetVertex(tet_id, i);
    vert_id_local_tvid[vid] = i;

    int fid = mesh.tetFace(tet_id, i);
    if (mesh.isBoundaryFace(fid)) {
      is_bnd_tet = true;
      is_bnd_face[i] = true;
    }
  }

  // prefactorize matrix
  Eigen::ColPivHouseholderQR<Eigen::MatrixXd> solver(A);
  std::vector<std::pair<Eigen::Vector3i, Eigen::Vector3d>> grid_pts;

  for (int k0 = std::ceil(min_vals[0]); k0 <= max_vals[0]; k0++) {
    for (int k1 = std::ceil(min_vals[1]); k1 <= max_vals[1]; k1++) {
      for (int k2 = std::ceil(min_vals[2]); k2 <= max_vals[2]; k2++) {
        Eigen::Vector3d b0;
        b0 << k0, k1, k2;
        b0 = b0 - b;
        Eigen::Vector3d sol = solver.solve(b0);
        Eigen::Vector4d bary(1 - sol[0] - sol[1] - sol[2], sol[0], sol[1], sol[2]);

        Eigen::RowVector3d p = (1 - sol[0] - sol[1] - sol[2]) * V.row(mesh.tetVertex(tet_id, 0));
        for (int j = 0; j < 3; j++) {
          p = p + sol[j] * V.row(mesh.tetVertex(tet_id, j + 1));
        }

        // inside the tet
        if (sol[0] > 0 && sol[1] > 0 && sol[2] > 0 && sol[0] + sol[1] + sol[2] < 1) {
          Eigen::Vector3d random_pt = SamplePointInsideTet(V, mesh, tet_id, true);  // random sample a point
          p = (1 - perturb_eps) * p + perturb_eps * random_pt.transpose();
          grid_pts.push_back({Eigen::Vector3i(k0, k1, k2), p.transpose()});
        } else if (is_bnd_tet) {
          double eps = 1e-6;
          // pretty close to boundary
          if (bary[0] > -eps && bary[1] > -eps && bary[2] > -eps && bary[3] > -eps) {
            // handle the boundary (kind of tricky)
            Eigen::Vector4d center_bary(0.25, 0.25, 0.25, 0.25);
            Eigen::Vector3d center = Eigen::Vector3d::Zero();
            for (int j = 0; j < 4; j++) {
              center += center_bary[j] * V.row(mesh.tetVertex(tet_id, j));
            }

            Eigen::Vector3d dir = p.transpose() - center;
            dir.normalize();

            // check the point is outside the mesh
            bool is_outside = false;
            for (int j = 0; j < 4; j++) {
              if (!is_bnd_face[j]) {
                continue;
              }
              int fid = mesh.tetFace(tet_id, j);

              Eigen::Vector3d v0 = V.row(mesh.faceVertex(fid, 0));
              Eigen::Vector3d v1 = V.row(mesh.faceVertex(fid, 1));
              Eigen::Vector3d v2 = V.row(mesh.faceVertex(fid, 2));

              double t = 0;

              bool is_intersect = IntersectTriangle(v0, v1, v2, center, dir, t, false);
              if (is_intersect && t > 0) {
                p = (center + 0.99 * t * dir).transpose();
                grid_pts.push_back({Eigen::Vector3i(k0, k1, k2), p.transpose()});
                break;
              }
            }
          }
        }
      }
    }
  }

  return grid_pts;
}

Eigen::Vector3d GetSingleFrame(const Eigen::MatrixXd& frames, int n_frames_per_tet, int tet_idx, int frame_idx) {
  assert(frame_idx < n_frames_per_tet);

  bool is_row_type = frames.cols() == n_frames_per_tet * 3;

  if (is_row_type) {
    return frames.row(tet_idx).segment<3>(3 * frame_idx).transpose();
  } else {
    return frames.row(n_frames_per_tet * tet_idx + frame_idx).transpose();
  }
}

bool AdvanceStreamline(const Eigen::MatrixXd& V, const TetMeshConnectivity& mesh, const Eigen::MatrixXd& frames,
                       Streamline& s, bool is_debug) {
  int cur_tet_id = s.tet_ids_.back();

  int nvecs = std::max(frames.rows() / mesh.nTets(), frames.cols() / 3);
  double stream_eps = s.stream_pts_.back().eps_;

  Eigen::MatrixXd cur_tet_frame(nvecs, 3);

  for (int i = 0; i < nvecs; i++) {
    cur_tet_frame.row(i) = GetSingleFrame(frames, nvecs, cur_tet_id, i).transpose();
  }

  Eigen::Vector3d cur_point = s.stream_pts_.back().start_pt_;
  Eigen::Vector3d prev_vec = s.stream_pts_.back().dir_;

  double max_match = 0;
  Eigen::Vector3d cur_vec = Eigen::Vector3d::Zero();
  for (int i = 0; i < nvecs; i++) {
    Eigen::Vector3d cur_dir = cur_tet_frame.row(i);

    // very tiny norm, skip
    if (cur_dir.norm() < 1e-10) {
      continue;
    }

    double cur_match = cur_dir.dot(prev_vec) / cur_dir.norm() / prev_vec.norm();

    if (std::abs(cur_match) > max_match) {
      max_match = std::abs(cur_match);
      cur_vec = cur_tet_frame.row(i);
      if (cur_match < 0) {
        cur_vec = -cur_vec;
      }
    }
  }
  if (cur_vec.norm() == 0) {
    if (is_debug) {
      std::cout << "All the frames are very small:\n" << cur_tet_frame << std::endl;
    }
    return false;
  }

  cur_vec.normalize();

  int cur_local_fid = s.tet_face_ids_.back();

  int next_tetface_id = -1;
  int next_face_id = -1;
  int next_tet_id = -1;
  Eigen::Vector3d intersect_point = cur_point;
  int next_face_orientation = -1;
  Eigen::Vector3d cur_face_normal = Eigen::Vector3d::Zero();

  for (int i = 0; i < 4; i++) {
    int fid = mesh.tetFace(cur_tet_id, i);
    Eigen::Vector3d v0, v1, v2;
    v0 << V(mesh.faceVertex(fid, 0), 0), V(mesh.faceVertex(fid, 0), 1), V(mesh.faceVertex(fid, 0), 2);
    v1 << V(mesh.faceVertex(fid, 1), 0), V(mesh.faceVertex(fid, 1), 1), V(mesh.faceVertex(fid, 1), 2);
    v2 << V(mesh.faceVertex(fid, 2), 0), V(mesh.faceVertex(fid, 2), 1), V(mesh.faceVertex(fid, 2), 2);

    if (i != cur_local_fid) {
      double intersect_time = 0;
      bool is_intersect = IntersectTriangle(v0, v1, v2, cur_point, cur_vec, intersect_time, false);

      if (is_intersect && intersect_time >= 0) {
        next_face_id = fid;
        intersect_point = cur_point + cur_vec * intersect_time;
        next_face_orientation = mesh.tetFaceOrientation(cur_tet_id, i);
        next_face_orientation = (next_face_orientation + 1) % 2;
        next_tet_id = mesh.faceTet(next_face_id, next_face_orientation);
      }
    } else {
      cur_face_normal = (v1 - v0).cross(v2 - v0);
      cur_face_normal.normalize();
    }
  }

  auto debug_output = [&]() {
    std::cout << "cur_tet_id: " << cur_tet_id << std::endl;
    std::cout << "cur_point: \n" << cur_point.transpose() << std::endl;
    std::cout << "prev_vec: \n" << prev_vec.transpose() << std::endl;
    std::cout << "cur_vec: \n" << cur_vec.transpose() << std::endl;
    std::cout << "cur_tet_frame: \n" << cur_tet_frame << std::endl;

    for (int j = 0; j < 4; j++) {
      std::cout << "cur tet v id: " << mesh.tetVertex(cur_tet_id, j)
                << ", pos: " << V.row(mesh.tetVertex(cur_tet_id, j)) << std::endl;
    }

    for (int j = 0; j < 4; j++) {
      std::cout << "cur tet face: " << mesh.tetFace(cur_tet_id, j)
                << ", vs: " << mesh.faceVertex(mesh.tetFace(cur_tet_id, j), 0) << ", "
                << mesh.faceVertex(mesh.tetFace(cur_tet_id, j), 1) << ", "
                << mesh.faceVertex(mesh.tetFace(cur_tet_id, j), 2) << std::endl;
    }

    bool is_pos = true;
    Eigen::Matrix3d A;
    A.row(0) = V.row(mesh.tetVertex(cur_tet_id, 1)) - V.row(mesh.tetVertex(cur_tet_id, 0));
    A.row(1) = V.row(mesh.tetVertex(cur_tet_id, 2)) - V.row(mesh.tetVertex(cur_tet_id, 0));
    A.row(2) = V.row(mesh.tetVertex(cur_tet_id, 3)) - V.row(mesh.tetVertex(cur_tet_id, 0));
    is_pos = A.determinant() > 0;
    std::cout << "positive orientation: " << is_pos << std::endl;

    if (s.tet_face_ids_.back() != -1) {
      int fid = mesh.tetFace(cur_tet_id, s.tet_face_ids_.back());

      int tet_id0 = mesh.faceTet(fid, 0);
      int tet_id1 = mesh.faceTet(fid, 1);
      bool is_on_bnd = (tet_id0 == -1 || tet_id1 == -1);

      Eigen::Vector3d v0, v1, v2;
      v0 << V(mesh.faceVertex(fid, 0), 0), V(mesh.faceVertex(fid, 0), 1), V(mesh.faceVertex(fid, 0), 2);
      v1 << V(mesh.faceVertex(fid, 1), 0), V(mesh.faceVertex(fid, 1), 1), V(mesh.faceVertex(fid, 1), 2);
      v2 << V(mesh.faceVertex(fid, 2), 0), V(mesh.faceVertex(fid, 2), 1), V(mesh.faceVertex(fid, 2), 2);

      std::cout << "Is boundary tet: " << is_on_bnd
                << "\nface normal dot product with pre vec: " << cur_face_normal.dot(prev_vec)
                << "\nface normal dot product with cur vec: " << cur_face_normal.dot(cur_vec) << std::endl;

      std::cout << "Is tracing point on the face (Expect 0): (p - v0).dot(n) = "
                << (cur_point - v0).dot(cur_face_normal) << std::endl;

      Eigen::Matrix<double, 3, 2> B;
      B.col(0) = v1 - v0;
      B.col(1) = v2 - v0;

      Eigen::Matrix2d BTB = B.transpose() * B;
      Eigen::Vector2d rhs = B.transpose() * (cur_point - v0);

      Eigen::Vector2d sol = BTB.inverse() * rhs;

      std::cout << "barycentric: " << 1 - sol[0] - sol[1] << " " << sol[0] << " " << sol[1] << std::endl;
    }

    for (int i = 0; i < 4; i++) {
      if (i != cur_local_fid) {
        int fid = mesh.tetFace(cur_tet_id, i);
        Eigen::Vector3d v0, v1, v2;
        v0 << V(mesh.faceVertex(fid, 0), 0), V(mesh.faceVertex(fid, 0), 1), V(mesh.faceVertex(fid, 0), 2);
        v1 << V(mesh.faceVertex(fid, 1), 0), V(mesh.faceVertex(fid, 1), 1), V(mesh.faceVertex(fid, 1), 2);
        v2 << V(mesh.faceVertex(fid, 2), 0), V(mesh.faceVertex(fid, 2), 1), V(mesh.faceVertex(fid, 2), 2);

        double intersect_time = 0;
        bool is_intersect = IntersectTriangle(v0, v1, v2, cur_point, cur_vec, intersect_time, true);
        std::cout << "face id: " << fid << std::endl;
        std::cout << "v0, id: " << mesh.faceVertex(fid, 0) << ", " << v0.transpose()
                  << "\nv1, id: " << mesh.faceVertex(fid, 1) << ", " << v1.transpose()
                  << "\nv2, id: " << mesh.faceVertex(fid, 2) << ", " << v2.transpose() << std::endl;
        std::cout << "intersect_time: " << intersect_time << ", is_intersect: " << is_intersect << std::endl;
      }
    }
  };

  // only for debug
  if (is_debug) {
    debug_output();
  }

  // This could happen: since the tracing pt is on the face (except for the initial case), if the cur_vec is not point
  // inside the tet, no intersection will happen Since prev_vec always points inside the tet, this happens if and only
  // if
  // 1. cur_vec is VERY different from the prev_vec, even it is the best-matched frame
  // 2. prev_vec is NEAR perpendicular to the face, and cur_vec may be very close to the prev_vec. But the error makes
  // from inward to outward.
  if (next_face_id == -1) {
    // intial case. There must be a numerical issue, since the tracing point is always inside the tet.
    if (cur_local_fid == -1) {
      if(is_debug) {
        std::cout << "Check the code for numerical issues!" << std::endl;
        debug_output();
      }
    } else {
      if (cur_face_normal.dot(prev_vec) * cur_face_normal.dot(cur_vec) > 0) {
        if(is_debug) {
          std::cout << "Stream line is not stop at the boundary and the prev_vec and cur_vec has same inward/outward "
                       "direction corresponding to the current tet"
                    << std::endl;
          debug_output();
        }
      }
    }
    return false;
  }

  s.stream_pts_.push_back(StreamPt(intersect_point, cur_vec, stream_eps, next_face_id));

  s.tet_ids_.push_back(next_tet_id);

  if (next_tet_id == -1) {
    // reach the boundary
    s.tet_face_ids_.push_back(-1);
    return false;
  } else {
    for (int i = 0; i < 4; i++) {
      int fid = mesh.tetFace(next_tet_id, i);
      if (fid == next_face_id) {
        next_tetface_id = i;
      }
    }
    s.tet_face_ids_.push_back(next_tetface_id);
  }
  return true;
}

// Tracing streamlines
void TraceStreamlines(const Eigen::MatrixXd& V, const TetMeshConnectivity& mesh, const Eigen::MatrixXd& frames,
                      int max_iter_per_trace, std::vector<Streamline>& traces, int num_seeds, bool is_random_inside,
                      double eps) {
  traces.clear();
  std::vector<std::pair<int, Eigen::Vector3d>> init_tet_ids =
      RandomSampleStreamPts(V, mesh, num_seeds, is_random_inside);
  std::vector<std::pair<int, StreamPt>> init_tracing_pts = {};

  int nstreamlines = init_tet_ids.size();

  int nvecs = std::max(frames.rows() / mesh.nTets(), frames.cols() / 3);
  std::unordered_set<StreamPt, StreamPt::HashFunction> visited_stream_pts;

  double max_norm = 0;
  for (int vec_id = 0; vec_id < nvecs; vec_id++) {
    for (int i = 0; i < nstreamlines; i++) {
      int cur_start_tet_id = init_tet_ids[i].first;
      //Eigen::Vector3d cur_direction_vec = frames.row(nvecs * cur_start_tet_id + vec_id);
      Eigen::Vector3d cur_direction_vec = GetSingleFrame(frames, nvecs, cur_start_tet_id, vec_id);
      max_norm = std::max(cur_direction_vec.norm(), max_norm);
    }
  }

  for (int vec_id = 0; vec_id < nvecs; vec_id++) {
    for (int orientation = 0; orientation < 2; orientation++) {
      int flip_sign = 1;
      if (orientation == 1) {
        flip_sign = -1;
      }
      for (int i = 0; i < nstreamlines; i++) {
        int cur_start_tet_id = init_tet_ids[i].first;
        //Eigen::Vector3d cur_direction_vec = frames.row(nvecs * cur_start_tet_id + vec_id);
        Eigen::Vector3d cur_direction_vec = GetSingleFrame(frames, nvecs, cur_start_tet_id, vec_id);
        cur_direction_vec *= flip_sign;
        if (cur_direction_vec.norm() < 0.05 * max_norm) {
          continue;
        }
        StreamPt cur_stream_pt(init_tet_ids[i].second, cur_direction_vec, eps, -1);
        init_tracing_pts.push_back({cur_start_tet_id, cur_stream_pt});
      }
    }
  }

  TraceStreamlines(V, mesh, frames, init_tracing_pts, max_iter_per_trace, traces);
}

void TraceStreamlines(const Eigen::MatrixXd& V, const TetMeshConnectivity& mesh, const Eigen::MatrixXd& frames,
                      const Eigen::MatrixXd& values, int max_iter_per_trace, std::vector<Streamline>& traces,
                      double eps, double perturb_eps) {
  if (values.cols() != 3) {
    std::cout << "the values doesn't has three channels, go back to use random sample for streamline tracing!"
              << std::endl;
    TraceStreamlines(V, mesh, frames, max_iter_per_trace, traces);
    return;
  }
  int ntets = mesh.nTets();
  int nvecs = std::max(frames.rows() / ntets, frames.cols() / 3);
  assert(frames.rows() == 3 * ntets || frames.cols() == 9);

  for (int j = 0; j < 3; j++) {
    std::cout << "channel j range: " << values.col(j).minCoeff() << ", " << values.col(j).maxCoeff() << std::endl;
  }

  std::vector<std::pair<int, StreamPt>> init_tracing_pts;

  // this can be parallelized
  for (int i = 0; i < ntets; i++) {
    std::vector<std::pair<Eigen::Vector3i, Eigen::Vector3d>> grid_pts = ComputeGridPts(V, mesh, values, i, perturb_eps);
    Eigen::Vector3d center = Eigen::Vector3d::Zero();
    if (!grid_pts.empty()) {
      for (auto& p : grid_pts) {
        for (int j = 0; j < nvecs; j++) {
          for (int orientation = 1; orientation >= -1; orientation -= 2) {
            //Eigen::Vector3d dir = orientation * frames.row(nvecs * i + j);
            Eigen::Vector3d dir = orientation * GetSingleFrame(frames, nvecs, i, j);
            StreamPt stream_pt(p.second, dir, eps, -1);
            init_tracing_pts.push_back({i, stream_pt});
          }
        }
      }
    }
  }

  TraceStreamlines(V, mesh, frames, init_tracing_pts, max_iter_per_trace, traces);
}

// Tracing streamlines if the tracing seeds (stream pts) are given
void TraceStreamlines(const Eigen::MatrixXd& V, const TetMeshConnectivity& mesh, const Eigen::MatrixXd& frames,
                      const std::vector<std::pair<int, StreamPt>>& init_tracing_pts, int max_iter_per_trace,
                      std::vector<Streamline>& traces) {
  int counter = 0;
  int nstreamlines = init_tracing_pts.size();
  traces.clear();

  std::unordered_map<StreamPt, int, StreamPt::HashFunction> visited_stream_pts;

  for (int i = 0; i < nstreamlines; i++) {
    int cur_start_tet_id = init_tracing_pts[i].first;

//    std::cout << "processing streamline: " << counter << std::endl;
    counter++;

    Streamline t;
    t.tet_ids_.push_back(cur_start_tet_id);
    t.tet_face_ids_.push_back(-1);

    StreamPt cur_stream_pt = init_tracing_pts[i].second;

    t.stream_pts_.push_back(cur_stream_pt);

    if (visited_stream_pts.count(cur_stream_pt)) {
      continue;
    }

    if (visited_stream_pts.count(cur_stream_pt)) {
      visited_stream_pts[cur_stream_pt] += 1;
    } else {
      visited_stream_pts[cur_stream_pt] = 1;
    }

    std::unordered_set<StreamTetDir, StreamTetDir::HashFunction> tet_dir_set;
    tet_dir_set.insert(StreamTetDir(cur_start_tet_id, cur_stream_pt.dir_));

    bool is_debug = false;

    for (int j = 0; j < max_iter_per_trace; j++) {
      if (AdvanceStreamline(V, mesh, frames, t, is_debug)) {
        StreamPt stream_pt = t.stream_pts_.back();
        StreamTetDir stream_tet_dir(t.tet_ids_.back(), stream_pt.dir_);

        // this tet has been visited and the tracing direction is the same, we need to stop to prevent
        // unnecessary loop
        if (tet_dir_set.count(stream_tet_dir)) {
          // has been visited
          t.tet_ids_.pop_back();
          t.tet_face_ids_.pop_back();
          t.stream_pts_.pop_back();
          break;
        } else {
          tet_dir_set.insert(stream_tet_dir);

          if (visited_stream_pts.count(stream_pt)) {
            // has been visited
            t.tet_ids_.pop_back();
            t.tet_face_ids_.pop_back();
            t.stream_pts_.pop_back();
            break;
          } else {
            visited_stream_pts[stream_pt] = 1;
          }
        }

      } else {
        break;
      }
    }
    if (t.stream_pts_.size() >= 2) {
      // actually it is a segment
      traces.push_back(t);
    }
  }
}

// Merge line segments in a streamline which are short enough (L2 len < len_eps) and parallel (the angle is smaller than
// angle_eps in radian)
Streamline MergeStreamline(const Streamline& s, double len_eps, double angle_eps) {
  Streamline merged_s;
  if (s.stream_pts_.size() < 2) {
    merged_s = s;
    return merged_s;
  }

  merged_s.push_back(s.tet_ids_[0], s.tet_face_ids_[0], s.stream_pts_[0]);
  merged_s.push_back(s.tet_ids_[1], s.tet_face_ids_[1], s.stream_pts_[1]);

  StreamPt cur_stream_pt = merged_s.stream_pts_[1], pre_stream_pt = merged_s.stream_pts_[0];

  for (int i = 2; i < s.stream_pts_.size(); i++) {
    StreamPt next_stream_pt = s.stream_pts_[i];
    double cur_len = (cur_stream_pt.start_pt_ - pre_stream_pt.start_pt_).norm();
    double next_len = (next_stream_pt.start_pt_ - cur_stream_pt.start_pt_).norm();

    bool is_short = false, is_parallel = false;

    // short line segments
    if (cur_len < len_eps || next_len < len_eps) {
      is_short = true;
    }

    if (!is_short) {
      // check parallelization
      // pre_stream_pt.dir_ gives the direction from previous pt to the current pt and cur_stream_pt.dir_ gives the
      // direction from the current to the next
      double cos = pre_stream_pt.dir_.dot(cur_stream_pt.dir_) / pre_stream_pt.dir_.norm() / cur_stream_pt.dir_.norm();
      double theta = std::acos(std::clamp(cos, -1.0, 1.0));  // use clamp to avoid numerical issues
      if (theta < angle_eps) {
        is_parallel = true;
      }
    }

    if (is_short || is_parallel) {
      // remove the current pt
      merged_s.pop_back();
    }

    // update the current point
    merged_s.push_back(s.tet_ids_[i], s.tet_face_ids_[i], next_stream_pt);
    pre_stream_pt = std::move(cur_stream_pt);
    cur_stream_pt = std::move(next_stream_pt);
  }

  return merged_s;
}
}  // namespace CubeCover