#ifndef SINGULARCURVENETWORK_H
#define SINGULARCURVENETWORK_H

#include <Eigen/Core>

namespace CubeCover
{
    class FrameField;
    class TetMeshConnectivity;
};


void extractSingularCurveNetwork(const Eigen::MatrixXd& V,
    const CubeCover::TetMeshConnectivity& mesh,
    const CubeCover::FrameField& field,
    Eigen::MatrixXd& Pgreen, Eigen::MatrixXi& Egreen,
    Eigen::MatrixXd& Pblue, Eigen::MatrixXi& Eblue,
    Eigen::MatrixXd& Pblack, Eigen::MatrixXi& Eblack
);

// 0: +1/4 singularity, 1: 1/4 singularity, 2: irregular singularity
std::vector<int> getSingularEdgeLabels(const Eigen::MatrixXd& V,
    const CubeCover::TetMeshConnectivity& mesh,
    const CubeCover::FrameField& field
);

#endif