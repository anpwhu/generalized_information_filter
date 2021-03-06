#include "gtest/gtest.h"

#include "../include/generalized_information_filter/element-vector.h"
#include "generalized_information_filter/binary-residual.h"
#include "generalized_information_filter/common.h"
#include "generalized_information_filter/filter.h"
#include "generalized_information_filter/prediction.h"
#include "generalized_information_filter/residuals/imu-prediction.h"
#include "generalized_information_filter/residuals/kinematics-model.h"
#include "generalized_information_filter/residuals/landmark-prediction.h"
#include "generalized_information_filter/residuals/leg-kinematic-update.h"
#include "generalized_information_filter/residuals/pose-update.h"
#include "generalized_information_filter/residuals/random-walk-prediction.h"
#include "generalized_information_filter/transformation.h"
#include "generalized_information_filter/unary-update.h"

using namespace GIF;

class TransformationExample : public Transformation<ElementPack<Vec3>,
    ElementPack<double, std::array<Vec3, 4>>> {
 public:
  TransformationExample()
      : mtTransformation({"pos"}, {"tim", "sta"}) {
  }

  virtual ~TransformationExample() {}

  void Transform(Vec3& posOut, const double& timeIn, const std::array<Vec3, 4>& posIn) const {
    posOut = (timeIn + 1.0) * (posIn[2] + Vec3(1, 2, 3));
  }
  void JacTransform(MatX& J, const double& timeIn,
                    const std::array<Vec3, 4>& posIn) const {
    J.setZero();
    GetJacBlock<0, 0>(J) = Vec3(1, 2, 3);
    Eigen::Matrix<double, 3, 12> J2;
    J2.setZero();
    J2.block<3, 3>(0, 6) = Mat3::Identity();
    GetJacBlock<0, 1>(J) = J2;
  }
};

class BinaryRedidualVelocity : public BinaryResidual<ElementPack<Vec3>,
    ElementPack<Vec3, Vec3>, ElementPack<Vec3>, ElementPack<Vec3>, EmptyMeas> {
 public:
  BinaryRedidualVelocity()
      : mtBinaryRedidual("velRes", { "pos" }, { "pos", "vel" }, { "pos" }, { "pos" },
                         false, false, false) {
    dt_ = 0.1;
  }

  virtual ~BinaryRedidualVelocity() {}

  void Eval(Vec3& posRes, const Vec3& posPre, const Vec3& velPre,
                        const Vec3& posCur, const Vec3& posNoi) const {
    posRes = posPre + dt_ * velPre - posCur + posNoi;
  }

  void JacPre(MatX& J, const Vec3& posPre, const Vec3& velPre,
                  const Vec3& posCur, const Vec3& posNoi) const {
    J.setZero();
    GetJacBlockPre<0, 0>(J) = Mat3::Identity();
    GetJacBlockPre<0, 1>(J) = dt_ * Mat3::Identity();
  }

  void JacCur(MatX& J, const Vec3& posPre, const Vec3& velPre,
                  const Vec3& posCur, const Vec3& posNoi) const {
    J.setZero();
    GetJacBlockCur<0, 0>(J) = -Mat3::Identity();
  }

  void JacNoi(MatX& J, const Vec3& posPre, const Vec3& velPre,
                  const Vec3& posCur, const Vec3& posNoi) const {
    J.setZero();
    GetJacBlockNoi<0, 0>(J) = Mat3::Identity();
  }

 protected:
  double dt_;
};

class AccelerometerMeas : public ElementVector {
 public:
  AccelerometerMeas(const Vec3& acc = Vec3(0, 0, 0))
      : ElementVector(std::shared_ptr<ElementVectorDefinition>(new ElementPack<Vec3>( { "acc" }))),
        acc_(ElementVector::GetValue<Vec3>("acc")) {
    acc_ = acc;
  }
  Vec3& acc_;
};

class BinaryRedidualAccelerometer : public BinaryResidual<ElementPack<Vec3>,
    ElementPack<Vec3>, ElementPack<Vec3>, ElementPack<Vec3>, AccelerometerMeas> {
 public:
  BinaryRedidualAccelerometer()
      : mtBinaryRedidual("accRes", { "vel" }, { "vel" }, { "vel" }, { "vel" }, false,
                         true, true) {
    dt_ = 0.1;
  }

  virtual ~BinaryRedidualAccelerometer() {
  }

  void Eval(Vec3& velRes, const Vec3& velPre, const Vec3& velCur,
                        const Vec3& velNoi) const {
    velRes = velPre + dt_ * meas_->acc_ - velCur + velNoi;
  }
  void JacPre(MatX& J, const Vec3& velPre, const Vec3& velCur,
                  const Vec3& velNoi) const {
    J.setZero();
    GetJacBlockPre<0, 0>(J) = Mat3::Identity();
  }
  void JacCur(MatX& J, const Vec3& velPre, const Vec3& velCur,
                  const Vec3& velNoi) const {
    J.setZero();
    GetJacBlockCur<0, 0>(J) = -Mat3::Identity();
  }
  void JacNoi(MatX& J, const Vec3& velPre, const Vec3& velCur,
                  const Vec3& velNoi) const {
    J.setZero();
    GetJacBlockNoi<0, 0>(J) = Mat3::Identity();
  }

 protected:
  double dt_;
};

class PredictionAccelerometer : public Prediction<ElementPack<Vec3>,
    ElementPack<Vec3>, AccelerometerMeas> {
 public:
  PredictionAccelerometer()
      : mtPrediction("accPre", { "vel" }, { "vel" }) {
    dt_ = 0.1;
  }

  virtual ~PredictionAccelerometer() {}

  void Predict(Vec3& velCur, const Vec3& velPre,
                          const Vec3& velNoi) const {
    velCur = velPre + dt_ * meas_->acc_ + velNoi;
  }
  void PredictJacPre(MatX& J, const Vec3& velPre,
                            const Vec3& velNoi) const {
    J.setZero();
    GetJacBlockPre<0, 0>(J) = Mat3::Identity();
  }
  void PredictJacNoi(MatX& J, const Vec3& velPre,
                            const Vec3& velNoi) const {
    J.setZero();
    GetJacBlockNoi<0, 0>(J) = Mat3::Identity();
  }

 protected:
  double dt_;
};

// The fixture for testing class ScalarState
class NewStateTest : public virtual ::testing::Test {
 protected:
  NewStateTest()
      : covMat_(1, 1) {}
  virtual ~NewStateTest() {}
  MatX covMat_;
};

// Test constructors
TEST_F(NewStateTest, constructor) {
  TransformationExample t;
  ElementVector s1a(t.InputDefinition());
  ElementVector s1b(t.InputDefinition());
  s1a.SetIdentity();
  std::cout << s1a.Print();

  // Boxplus and BoxMinus
  Eigen::VectorXd v(s1a.GetDim());
  v.setZero();
  for (int i = 0; i < s1a.GetDim(); i++) {
    v(i) = i;
  }
  s1a.BoxPlus(v, &s1b);
  std::cout << s1b.Print();
  s1a.BoxMinus(s1b, v);
  std::cout << v.transpose() << std::endl;

  // Jacobian
  MatX J;
  t.JacFD(J, s1a, 1e-6);
  std::cout << J << std::endl;

  // Transformation
  ElementVector s2(t.OutputDefinition());
  MatX P1(s1a.GetDim(), s1a.GetDim());
  MatX P2(s2.GetDim(), s2.GetDim());
  t.TransformState(&s2, s1a);
  t.TransformCovMat(P2, s1a, P1);
  t.JacTest(s1a, 1e-6, 1e-6);

  // Velocity Residual
  std::shared_ptr<BinaryRedidualVelocity> velRes(new BinaryRedidualVelocity());
  ElementVector pre(velRes->PreDefinition());
  pre.SetIdentity();
  ElementVector cur(velRes->CurDefinition());
  cur.SetIdentity();
  ElementVector noi(velRes->NoiDefinition());
  noi.SetIdentity();
  velRes->TestJacs(pre, cur, noi, 1e-6, 1e-6);

  // Accelerometer Residual
  std::shared_ptr<BinaryRedidualAccelerometer> accRes(new BinaryRedidualAccelerometer());

  // Filter
  Filter f;
  f.AddResidual(velRes,fromSec(0.1),fromSec(0.0));
  f.AddResidual(accRes,fromSec(0.1),fromSec(0.0));
  std::cout << f.PrintConnectivity();
  ElementVector preState(f.StateDefinition());
  preState.SetIdentity();
  preState.GetValue < Vec3 > ("pos") = Vec3(1, 2, 3);
  std::cout << preState.Print();
  ElementVector curState(f.StateDefinition());
  curState.SetIdentity();
  std::cout << curState.Print();


  // Test measurements
  std::shared_ptr<EmptyMeas> eptMeas(new EmptyMeas);
  TimePoint start = Clock::now();
  f.AddMeasurement(0, eptMeas,start+fromSec(-0.1));
  f.AddMeasurement(0, eptMeas,start+fromSec(0.0));
  f.AddMeasurement(0, eptMeas,start+fromSec(0.2));
  f.AddMeasurement(0, eptMeas,start+fromSec(0.3));
  f.AddMeasurement(0, eptMeas,start+fromSec(0.4));
  f.AddMeasurement(1, std::shared_ptr<AccelerometerMeas>(
      new AccelerometerMeas(Vec3(-0.1,0.0,0.0))),start+fromSec(-0.1));
  f.AddMeasurement(1, std::shared_ptr<AccelerometerMeas>(
      new AccelerometerMeas(Vec3(0.0,0.0,0.0))),start+fromSec(0.0));
  f.AddMeasurement(1, std::shared_ptr<AccelerometerMeas>(
      new AccelerometerMeas(Vec3(0.1,0.0,0.0))),start+fromSec(0.1));
  f.AddMeasurement(1, std::shared_ptr<AccelerometerMeas>(
      new AccelerometerMeas(Vec3(0.4,0.0,0.0))),start+fromSec(0.3));
  f.AddMeasurement(1, std::shared_ptr<AccelerometerMeas>(
      new AccelerometerMeas(Vec3(0.3,0.0,0.0))),start+fromSec(0.5));

  f.Update();

  f.Update();

  // Prediction Accelerometer
  std::shared_ptr<PredictionAccelerometer> accPre(new PredictionAccelerometer());
  ElementVector preAcc(accPre->PreDefinition());
  ElementVector curAcc(accPre->PreDefinition());
  ElementVector noiAcc(accPre->NoiDefinition());
  preAcc.SetIdentity();
  curAcc.SetIdentity();
  noiAcc.SetIdentity();
  accPre->TestJacs(preAcc, curAcc, noiAcc, 1e-6, 1e-6);

  // Test measurements
  Filter f2;
  f2.AddResidual(velRes,fromSec(0.1),fromSec(0.0));
  f2.AddResidual(accPre,fromSec(0.1),fromSec(0.0));
  std::cout << f2.PrintConnectivity();
  f2.AddMeasurement(0,eptMeas,start+fromSec(-0.1));
  f2.AddMeasurement(0,eptMeas,start+fromSec(0.0));
  f2.AddMeasurement(0,eptMeas,start+fromSec(0.2));
  f2.AddMeasurement(0,eptMeas,start+fromSec(0.3));
  f2.AddMeasurement(0,eptMeas,start+fromSec(0.4));
  f2.AddMeasurement(1,std::shared_ptr<AccelerometerMeas>(
      new AccelerometerMeas(Vec3(-0.1,0.0,0.0))),start+fromSec(-0.1));
  f2.AddMeasurement(1,std::shared_ptr<AccelerometerMeas>(
      new AccelerometerMeas(Vec3(0.0,0.0,0.0))),start+fromSec(0.0));
  f2.AddMeasurement(1,std::shared_ptr<AccelerometerMeas>(
      new AccelerometerMeas(Vec3(0.1,0.0,0.0))),start+fromSec(0.1));
  f2.AddMeasurement(1,std::shared_ptr<AccelerometerMeas>(
      new AccelerometerMeas(Vec3(0.4,0.0,0.0))),start+fromSec(0.3));
  f2.AddMeasurement(1,std::shared_ptr<AccelerometerMeas>(
      new AccelerometerMeas(Vec3(0.3,0.0,0.0))),start+fromSec(0.5));
  f2.Update();
  f2.Update();

  // Test Imu + Pose filter
  std::shared_ptr<ImuPrediction> imuPre(new ImuPrediction("ImuPre"));
  imuPre->GetNoiseCovariance() = 1e-8 * imuPre->GetNoiseCovariance();
  imuPre->TestJacs(1e-6, 1e-6);
  std::shared_ptr<PoseUpdate> poseUpd(new PoseUpdate("PoseUpd", {"JrJC", "qCJ"},
                                                     {"IrIM", "qIM", "IrIJ", "qIJ"},
                                                     {"JrJC", "qCJ"}));
  poseUpd->GetNoiseCovariance() = 1e-8 * poseUpd->GetNoiseCovariance();
  poseUpd->TestJacs(1e-6, 1e-6);
  std::shared_ptr<RandomWalkPrediction<ElementPack<Vec3,Quat>>> extPre(
      new RandomWalkPrediction<ElementPack<Vec3,Quat>>("ExtPre", {"IrIJ", "qIJ"},{"IrIJ", "qIJ"}));
  extPre->GetNoiseCovariance() = 1e-8 * extPre->GetNoiseCovariance();
  extPre->TestJacs(1e-6, 1e-6);

  Filter imuPoseFilter;
  int imuPreInd = imuPoseFilter.AddResidual(imuPre,fromSec(0.1),fromSec(0.0));
  int textPreInd = imuPoseFilter.AddResidual(extPre,fromSec(0.1),fromSec(0.0));
  int poseUpdInd = imuPoseFilter.AddResidual(poseUpd,fromSec(0.1),fromSec(0.0));
  std::cout << imuPoseFilter.PrintConnectivity();
  imuPoseFilter.AddMeasurement(imuPreInd, std::shared_ptr<ImuMeas>(
      new ImuMeas(Vec3(0.0, 0.0, 0.0), Vec3(0.0, 0.0, 9.81))), start);
  imuPoseFilter.Update();
  for (int i = 1; i <= 10; i++) {
    imuPoseFilter.AddMeasurement(imuPreInd, std::shared_ptr<ImuMeas>(
        new ImuMeas(Vec3(0.3, 0.0, 0.1), Vec3(0.0, 0.2, 9.81))),
        start + fromSec(0.1*i));
    std::cout << "Update1 " << i << std::endl;
    imuPoseFilter.Update();
    imuPoseFilter.AddMeasurement(poseUpdInd, std::shared_ptr<PoseMeas>(
        new PoseMeas(Vec3(0.0, 0.0, 0.0), Quat(1.0, 0.0, 0.0, 0.0))),
        start + fromSec(0.05+0.1*i));
    imuPoseFilter.AddMeasurement(textPreInd, std::make_shared<EmptyMeas>(),
        start + fromSec(0.05+0.1*i));
    std::cout << "Update2 " << i << std::endl;
    imuPoseFilter.Update();
  }
  TimePoint startFilter = Clock::now();
  imuPoseFilter.Update();
  std::cout << toSec(Clock::now()-startFilter)*1000 << " ms" << std::endl;

  // Test Landmark Prediction
  std::shared_ptr<RobocentricLandmarkPrediction<4>> rcLMPre(new RobocentricLandmarkPrediction<4>("rcLMPre"));
  rcLMPre->TestJacs(1e-6,1e-6);

  // Test Leg Kinematic Update
  std::shared_ptr<LegKinematicUpdate<LeggedRobotModelExample>> legKinematicUpd(new LegKinematicUpdate<LeggedRobotModelExample>("legKinematicUpd"));
  std::shared_ptr<LeggedRobotModelExample> model(new LeggedRobotModelExample());
  legKinematicUpd->SetModelPtr(model);
  legKinematicUpd->TestJacs(1e-6,1e-6);
}

int main(int argc, char **argv) {
  google::InitGoogleLogging(argv[0]);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
