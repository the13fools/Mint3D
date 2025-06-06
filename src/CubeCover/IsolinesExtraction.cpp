#include "IsolinesExtraction.h"

#include <cassert>
#include <vector>
#include <iostream>

#include <Eigen/Dense>

namespace CubeCover {
struct Segment
{
  Eigen::Vector3d end1;
  Eigen::Vector3d end2;
};

void ExtractIsolines(const Eigen::MatrixXd& V, const CubeCover::TetMeshConnectivity& mesh, const Eigen::MatrixXd& values,
                     Eigen::MatrixXd& P,
                     Eigen::MatrixXi& E,
                     Eigen::MatrixXd& P2,
                     Eigen::MatrixXi& E2)
{
  constexpr double clamptol = 1e-6;

  assert(values.rows() == mesh.nTets() * 4);
  assert(values.cols() == 3);

  std::vector<Segment> segs;
  std::vector<Segment> segs2;

  int ntets = mesh.nTets();
  for (int i = 0; i < ntets; i++)
  {
    double minvals[3];
    double maxvals[3];
    for (int j = 0; j < 3; j++)
    {
      minvals[j] = std::numeric_limits<double>::infinity();
      maxvals[j] = -std::numeric_limits<double>::infinity();
    }
    for (int j = 0; j < 4; j++)
    {
      for (int k = 0; k < 3; k++)
      {
        minvals[k] = std::min(minvals[k], values(4 * i + j, k));
        maxvals[k] = std::max(maxvals[k], values(4 * i + j, k));
      }
    }

    int tosamplelower[3];
    int tosampleupper[3];

    for (int j = 0; j < 3; j++)
    {
      tosamplelower[j] = std::ceil(minvals[j]);
      tosampleupper[j] = std::floor(maxvals[j]);
    }

    // add segments for each direction
    for (int dir = 0; dir < 3; dir++)
    {
      int sample1 = (dir + 1) % 3;
      int sample2 = (dir + 2) % 3;
      for (int val1 = tosamplelower[sample1]; val1 <= tosampleupper[sample1]; val1++)
      {
        for (int val2 = tosamplelower[sample2]; val2 <= tosampleupper[sample2]; val2++)
        {
          std::vector<Eigen::Vector4d> pts;
          int nvertpts = 0;
          int nedgepts = 0;
          int nfacepts = 0;

          // face case
          for (int vert1 = 0; vert1 < 4; vert1++)
          {
            for (int vert2 = vert1 + 1; vert2 < 4; vert2++)
            {
              for (int vert3 = vert2 + 1; vert3 < 4; vert3++)
              {
                double s11val = values(4 * i + vert1, sample1);
                double s12val = values(4 * i + vert2, sample1);
                double s13val = values(4 * i + vert3, sample1);
                double s21val = values(4 * i + vert1, sample2);
                double s22val = values(4 * i + vert2, sample2);
                double s23val = values(4 * i + vert3, sample2);

                Eigen::Matrix2d M;
                M << s12val - s11val, s13val - s11val,
                    s22val - s21val, s23val - s21val;

                if (M.determinant() == 0)
                {
                  // std::cout << "zero volume" <<std::endl;
                  // Segment s;
                  // s.end1 = V.row( mesh.tetVertex(i, vert1) );
                  // s.end2 = V.row( mesh.tetVertex(i, vert2) );;
                  // segs2.push_back(s);
                  // s.end1 = V.row( mesh.tetVertex(i, vert2) );
                  // s.end2 = V.row( mesh.tetVertex(i, vert3) );;
                  // segs2.push_back(s);
                  // s.end1 = V.row( mesh.tetVertex(i, vert1) );
                  // s.end2 = V.row( mesh.tetVertex(i, vert3) );;
                  // segs2.push_back(s);
                  continue;
                }

                Eigen::Vector2d rhs(double(val1) - s11val, double(val2) - s21val);
                Eigen::Vector2d barys = M.inverse() * rhs;
                if (barys[0] >= -clamptol && barys[1] >= -clamptol && barys[0] + barys[1] <= 1.0 + clamptol)
                {
                  Eigen::Vector4d pt(0, 0, 0, 0);
                  pt[vert1] = 1.0 - barys[0] - barys[1];
                  pt[vert2] = barys[0];
                  pt[vert3] = barys[1];
                  pts.push_back(pt);
                  nfacepts++;
                }
              }
            }
          }

          // clamp together identical pts
          std::vector<int> tokeep;
          int ncrossings = pts.size();
          for (int j = 0; j < ncrossings; j++)
          {
            bool ok = true;
            for (int k = 0; k < j; k++)
            {
              if ((pts[j] - pts[k]).norm() < clamptol)
                ok = false;
            }
            if (ok)
              tokeep.push_back(j);
          }

          // LOG A BUNCH OF STUFF CAUSE WHY IS THIS HAPPENING???
          if (tokeep.size() > 2)
          {
            std::cerr << "warning: bad tetrahedron " << i << std::endl;

            // for (int j = 0; j < ncrossings; j++)
            // {
            //   for (int k = 0; k < j; k++)
            //   {
            //     if ((pts[j] - pts[k]).norm() < clamptol)
            //     {
            //       std::cout << "pts[j]:" << pts[j] << " j: " << j << " pts[k]: " << pts[k] << " k: " << k << std::endl;

            //     }
            //   }
            // }

            // for (int vert1 = 0; vert1 < 4; vert1++)
            // {
            //   for (int vert2 = vert1 + 1; vert2 < 4; vert2++)
            //   {
            //     for (int vert3 = vert2 + 1; vert3 < 4; vert3++)
            //     {
            //       double s11val = values(4 * i + vert1, sample1);
            //       double s12val = values(4 * i + vert2, sample1);
            //       double s13val = values(4 * i + vert3, sample1);
            //       double s21val = values(4 * i + vert1, sample2);
            //       double s22val = values(4 * i + vert2, sample2);
            //       double s23val = values(4 * i + vert3, sample2);

            //       Eigen::Matrix2d M;
            //       M << s12val - s11val, s13val - s11val,
            //           s22val - s21val, s23val - s21val;

            //       std::cout << "M: " << M << std::endl;
            //       std::cout << "det: " << M.determinant() << std::endl;
            //       std::cout << "s11val: " << s11val << " s12val: " << s12val << " s13val: " << s13val << " s21val: " << s21val << " s22val: " << s22val << " s23val: " << s23val << std::endl;
            //     }
            //   }
            // }


          }
          for (int pt1 = 0; pt1 < tokeep.size(); pt1++)
          {
            for (int pt2 = pt1 + 1; pt2 < tokeep.size(); pt2++)
            {
              Eigen::Vector3d ambpt[2];
              ambpt[0].setZero();
              ambpt[1].setZero();
              for (int j = 0; j < 4; j++)
              {
                ambpt[0] += pts[tokeep[pt1]][j] * V.row(mesh.tetVertex(i, j));
                ambpt[1] += pts[tokeep[pt2]][j] * V.row(mesh.tetVertex(i, j));
              }
              Segment s;
              s.end1 = ambpt[0];
              s.end2 = ambpt[1];
              segs.push_back(s);
            }
          }
        }
      }
    }
  }

  int nsegs = segs.size();
  P.resize(2 * nsegs, 3);
  E.resize(nsegs, 2);
  for (int i = 0; i < nsegs; i++)
  {
    P.row(2 * i) = segs[i].end1;
    P.row(2 * i + 1) = segs[i].end2;
    E(i, 0) = 2 * i;
    E(i, 1) = 2 * i + 1;
  }

  int nsegs2 = segs2.size();
  P2.resize(2 * nsegs2, 3);
  E2.resize(nsegs2, 2);
  for (int i = 0; i < nsegs2; i++)
  {
    P2.row(2 * i) = segs2[i].end1;
    P2.row(2 * i + 1) = segs2[i].end2;
    E2(i, 0) = 2 * i;
    E2(i, 1) = 2 * i + 1;
  }

}
}