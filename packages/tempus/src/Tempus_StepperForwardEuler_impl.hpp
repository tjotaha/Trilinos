// @HEADER
// ****************************************************************************
//                Tempus: Copyright (2017) Sandia Corporation
//
// Distributed under BSD 3-clause license (See accompanying file Copyright.txt)
// ****************************************************************************
// @HEADER

#ifndef Tempus_StepperForwardEuler_impl_hpp
#define Tempus_StepperForwardEuler_impl_hpp

#include "Teuchos_VerboseObjectParameterListHelpers.hpp"
#include "Teuchos_TimeMonitor.hpp"
#include "Thyra_VectorStdOps.hpp"

namespace Tempus {

// StepperForwardEuler definitions:
template<class Scalar>
StepperForwardEuler<Scalar>::StepperForwardEuler(
  Teuchos::RCP<Teuchos::ParameterList>                      pList,
  const Teuchos::RCP<const Thyra::ModelEvaluator<Scalar> >& transientModel )
{
  // Set all the input parameters and call initialize
  this->setParameterList(pList);
  this->setModel(transientModel);
  this->initialize();
}

template<class Scalar>
void StepperForwardEuler<Scalar>::setModel(
  const Teuchos::RCP<const Thyra::ModelEvaluator<Scalar> >& transientModel)
{
  this->validExplicitODE(transientModel);
  eODEModel_ = transientModel;

  inArgs_  = eODEModel_->createInArgs();
  outArgs_ = eODEModel_->createOutArgs();
  inArgs_  = eODEModel_->getNominalValues();
}

template<class Scalar>
void StepperForwardEuler<Scalar>::setNonConstModel(
  const Teuchos::RCP<Thyra::ModelEvaluator<Scalar> >& transientModel)
{
  this->setModel(transientModel);
}

template<class Scalar>
void StepperForwardEuler<Scalar>::takeStep(
  const Teuchos::RCP<SolutionHistory<Scalar> >& solutionHistory)
{
  using Teuchos::RCP;

  TEMPUS_FUNC_TIME_MONITOR("Tempus::StepperForwardEuler::takeStep()");
  {
    RCP<SolutionState<Scalar> > currentState=solutionHistory->getCurrentState();

    typedef Thyra::ModelEvaluatorBase MEB;
    inArgs_.set_x(currentState->getX());
    if (inArgs_.supports(MEB::IN_ARG_t)) inArgs_.set_t(currentState->getTime());

    // For model evaluators whose state function f(x, x_dot, t) describes
    // an implicit ODE, and which accept an optional x_dot input argument,
    // make sure the latter is set to null in order to request the evaluation
    // of a state function corresponding to the explicit ODE formulation
    // x_dot = f(x, t)
    if (inArgs_.supports(MEB::IN_ARG_x_dot)) inArgs_.set_x_dot(Teuchos::null);
    outArgs_.set_f(currentState->getXDot());

    eODEModel_->evalModel(inArgs_,outArgs_);

    // Forward Euler update, x = x + dt*xdot
    RCP<SolutionState<Scalar> > workingState=solutionHistory->getWorkingState();
    const Scalar dt = workingState->getTimeStep();
    Thyra::V_VpStV(Teuchos::outArg(*(workingState->getX())),
      *(currentState->getX()),dt,*(currentState->getXDot()));

    workingState->stepperState_->stepperStatus_ = Status::PASSED;
  }
  return;
}


/** \brief Provide a StepperState to the SolutionState.
 *  This Stepper does not have any special state data,
 *  so just provide the base class StepperState with the
 *  Stepper description.  This can be checked to ensure
 *  that the input StepperState can be used by this Stepper.
 */
template<class Scalar>
Teuchos::RCP<Tempus::StepperState<Scalar> > StepperForwardEuler<Scalar>::
getDefaultStepperState()
{
  Teuchos::RCP<Tempus::StepperState<Scalar> > stepperState =
    rcp(new StepperState<Scalar>(description()));
  return stepperState;
}


template<class Scalar>
std::string StepperForwardEuler<Scalar>::description() const
{
  std::string name = "Forward Euler";
  return(name);
}


template<class Scalar>
void StepperForwardEuler<Scalar>::describe(
   Teuchos::FancyOStream               &out,
   const Teuchos::EVerbosityLevel      verbLevel) const
{
  out << description() << "::describe:" << std::endl
      << "eODEModel_ = " << eODEModel_->description() << std::endl;
}


template <class Scalar>
void StepperForwardEuler<Scalar>::setParameterList(
  const Teuchos::RCP<Teuchos::ParameterList> & pList)
{
  TEUCHOS_TEST_FOR_EXCEPT(is_null(pList));
  //pList->validateParameters(*this->getValidParameters());
  pList_ = pList;

  std::string stepperType = pList_->get<std::string>("Stepper Type");
  TEUCHOS_TEST_FOR_EXCEPTION( stepperType != "Forward Euler",
    std::logic_error,
       "Error - Stepper Type is not 'Forward Euler'!\n"
    << "  Stepper Type = "<< pList->get<std::string>("Stepper Type") << "\n");

  Teuchos::readVerboseObjectSublist(&*pList_,this);
}


template<class Scalar>
Teuchos::RCP<const Teuchos::ParameterList>
StepperForwardEuler<Scalar>::getValidParameters() const
{
  static Teuchos::RCP<Teuchos::ParameterList> validPL;

  if (is_null(validPL)) {

    Teuchos::RCP<Teuchos::ParameterList> pl = Teuchos::parameterList();
    Teuchos::setupVerboseObjectSublist(&*pl);

    std::ostringstream tmp;
    tmp << "'Stepper Type' must be 'Forward Euler'.";
    pl->set("Stepper Type", "Forward Euler", tmp.str());

    validPL = pl;
  }
  return validPL;
}


template <class Scalar>
Teuchos::RCP<Teuchos::ParameterList>
StepperForwardEuler<Scalar>::getNonconstParameterList()
{
  return(pList_);
}


template <class Scalar>
Teuchos::RCP<Teuchos::ParameterList>
StepperForwardEuler<Scalar>::unsetParameterList()
{
  Teuchos::RCP<Teuchos::ParameterList> temp_plist = pList_;
  pList_ = Teuchos::null;
  return(temp_plist);
}


} // namespace Tempus
#endif // Tempus_StepperForwardEuler_impl_hpp
