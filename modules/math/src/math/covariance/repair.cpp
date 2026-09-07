/// \file
/// Implementation of the repair primitives declared in
/// <math/covariance/repair.hpp>.

#include <math/covariance/repair.hpp>

#include <Eigen/Eigenvalues>

namespace mms::math::covariance {

namespace {

struct ReportConfiguration {
    double min_before = 0.0;
    double min_after = 0.0;
    int iterations = 0;
};

double smallest_eigenvalue(const Matrix& a) {
    return Eigen::SelfAdjointEigenSolver<Matrix>(a, Eigen::EigenvaluesOnly)
        .eigenvalues()
        .minCoeff();
}

RepairReport
make_report(const Matrix& input, const Matrix& output, ReportConfiguration configuration) {
    const Matrix diff = output - input;
    RepairReport r;
    r.frobenius_distance = diff.norm();
    r.min_eigenvalue_before = configuration.min_before;
    r.min_eigenvalue_after = configuration.min_after;
    r.max_abs_entry_change = diff.cwiseAbs().maxCoeff();
    r.iterations = configuration.iterations;
    return r;
}

} // namespace

RepairResult clip_eigenvalues(const Matrix& a, double epsilon) {
    const Eigen::SelfAdjointEigenSolver<Matrix> solver(a);
    const Vector& evals = solver.eigenvalues();
    const Matrix& evecs = solver.eigenvectors();

    const double min_before = evals.minCoeff();
    const Vector clipped = evals.cwiseMax(epsilon);

    Matrix repaired = evecs * clipped.asDiagonal() * evecs.transpose();
    repaired = ((repaired + repaired.transpose()) * 0.5).eval();

    ReportConfiguration configuration{};
    configuration.min_before = min_before;
    configuration.min_after = clipped.minCoeff();

    auto report = make_report(a, repaired, configuration);
    RepairResult result{repaired, report};
    return result;
}

RepairResult
nearest_correlation_higham(const Matrix& a, double min_eigenvalue, double tol, int max_iterations) {
    const Index n = a.rows();
    const double min_before = smallest_eigenvalue(a);

    Matrix y = a;
    Matrix delta_s = Matrix::Zero(n, n);

    int iterations = 0;
    for (; iterations < max_iterations; ++iterations) {
        const Matrix r = y - delta_s;
        const Matrix x = clip_eigenvalues(r, min_eigenvalue).matrix;
        delta_s = x - r;

        Matrix y_next = x;
        y_next.diagonal().setOnes();

        const double denom = y_next.norm();
        const double rel_change = denom > 0.0 ? (y_next - y).norm() / denom : 0.0;
        y = y_next;

        if (rel_change < tol) {
            ++iterations;
            break;
        }
    }

    ReportConfiguration configuration{};
    configuration.min_before = min_before;
    configuration.min_after = smallest_eigenvalue(y);
    configuration.iterations = iterations;

    auto report = make_report(a, y, configuration);
    RepairResult result{y, report};

    return result;
}

} // namespace mms::math::covariance