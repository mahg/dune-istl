// -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi: set et ts=4 sw=2 sts=2:
#ifndef DUNE_ISTL_TWOLEVELMETHOD_HH
#define DUNE_ISTL_TWOLEVELMETHOD_HH

#include<dune/istl/operators.hh>
#include"amg.hh"
#include"galerkin.hh"
#include<dune/istl/solver.hh>
#include<dune/common/shared_ptr.hh>

/**
 * @addtogroup ISTL_PAAMG
 * @{
 * @file
 * @author Markus Blatt
 * @brief Algebraic twolevel methods.
 */
namespace Dune
{
namespace Amg
{

/**
 * @brief Abstract base class for transfer between levels and creation
 * of the coarse level system.
 *
 * @tparam FO The type of the linear operator of the finel level system. Has to be
 * derived from AssembledLinearOperator.
 * @tparam CO The type of the linear operator of the coarse level system. Has to be
 * derived from AssembledLinearOperator.
 */
template<class FO, class CO>
class LevelTransferPolicy
{
public:
  /**
   * @brief The linear operator of the finel level system. Has to be
   * derived from AssembledLinearOperator.
   */
  typedef FO FineOperatorType;
  /**
   * @brief The type of the range of the fine level operator.
   */
  typedef typename FineOperatorType::range_type FineRangeType;
  /**
   * @brief The type of the domain of the fine level operator.
   */
  typedef typename FineOperatorType::domain_type FineDomainType;
  /**
   * @brief The linear operator of the finel level system. Has to be
   * derived from AssembledLinearOperator.
   */
  typedef CO CoarseOperatorType;
  /**
   * @brief The type of the range of the coarse level operator.
   */
  typedef typename CoarseOperatorType::range_type CoarseRangeType;
  /**
   * @brief The type of the domain of the coarse level operator.
   */
  typedef typename CoarseOperatorType::domain_type CoarseDomainType;
  /**
   * @brief Get the coarse level operator.
   * @return A shared pointer to the coarse level system.
   */
  shared_ptr<CoarseOperatorType>& getCoarseLevelOperator()
  {
    return operator_;
  }
  /**
   * @brief Get the coarse level right hand side.
   * @return The coarse level right hand side.
   */
  CoarseRangeType& getCoarseLevelRhs()
  {
    return rhs_;
  }

  /**
   * @brief Get the coarse level left hand side.
   * @return The coarse level leftt hand side.
   */
  CoarseDomainType& getCoarseLevelLhs()
  {
    return lhs_;
  }
  /**
   * @brief Transfers the data to the coarse level.
   *
   * Restricts the residual to the right hand side of the
   * coarse level system and initialies the left hand side
   * of the coarse level system. These can afterwards be accessed
   * usinf getCoarseLevelRhs() and getCoarseLevelLhs().
   * @param fineDefect The current residual of the fine level system.
   */
  virtual void moveToCoarseLevel(const FineRangeType& fineRhs)=0;
  /**
   * @brief Updates the fine level linear system after the correction
   * of the coarse levels system.
   *
   * After returning from this function the coarse level correction
   * will have been added to fine level system.
   * @param[inout] fineLhs The left hand side of the fine level to update
   * with the coarse level correction.
   */
  virtual void moveToFineLevel(FineDomainType& fineLhs)=0;
  /**
   * @brief Algebraically creates the coarse level system.
   *
   * After returning from this function the coarse level operator
   * can be accessed using getCoarseLevelOperator().
   * @param fineOperator The operator of the fine level system.
   */
  virtual void createCoarseLevelSystem(const FineOperatorType& fineOperator)=0;

  protected:
  /** @brief The coarse level rhs. */
  CoarseRangeType rhs_;
  /** @brief The coarse level lhs. */
  CoarseDomainType lhs_;
  /** @brief the coarse level linear operator. */
  shared_ptr<CoarseOperatorType> operator_;
};

/**
 * @brief A LeveTransferPolicy that used aggregation to construct the coarse level system.
 * @tparam O The type of the fine and coarse level operator.
 * @tparam C The criterion that describes the aggregation procedure.
 */
template<class O, class C>
class AggregationLevelTransferPolicy
  : public LevelTransferPolicy<O,O>
{
  typedef Dune::Amg::AggregatesMap<typename O::matrix_type::size_type> AggregatesMap;
public:
  typedef LevelTransferPolicy<O,O> FatherType;
  typedef C Criterion;
  typedef SequentialInformation ParallelInformation;

  AggregationLevelTransferPolicy(const Criterion& crit)
  : criterion_(crit)
  {}

  void createCoarseLevelSystem(const O& fineOperator)
  {
    prolongDamp_ = criterion_.getProlongationDampingFactor();
    GalerkinProduct<ParallelInformation> productBuilder;
    typedef typename Dune::Amg::MatrixGraph<const typename O::matrix_type> MatrixGraph;
    typedef typename Dune::Amg::PropertiesGraph<MatrixGraph,Dune::Amg::VertexProperties,
      Dune::Amg::EdgeProperties,Dune::IdentityMap,Dune::IdentityMap> PropertiesGraph;
    MatrixGraph mg(fineOperator.getmat());
    PropertiesGraph pg(mg,Dune::IdentityMap(),Dune::IdentityMap());
    typedef typename PropertiesGraph::VertexDescriptor Vertex;
    typedef NegateSet<typename ParallelInformation::OwnerSet> OverlapFlags;

    aggregatesMap_.reset(new AggregatesMap(pg.maxVertex()+1));

    int noAggregates, isoAggregates, oneAggregates, skippedAggregates;

     tie(noAggregates, isoAggregates, oneAggregates, skippedAggregates) =
       aggregatesMap_->buildAggregates(fineOperator.getmat(), pg, criterion_, true);
     std::cout<<"no aggregates="<<noAggregates<<" iso="<<isoAggregates<<" one="<<oneAggregates<<" skipped="<<skippedAggregates<<std::endl;
    // misuse coarsener to renumber aggregates
    Dune::Amg::IndicesCoarsener<Dune::Amg::SequentialInformation,int> renumberer;
    typedef std::vector<bool>::iterator Iterator;
    typedef Dune::IteratorPropertyMap<Iterator, Dune::IdentityMap> VisitedMap;
    std::vector<bool> excluded(fineOperator.getmat().N(), false);
    VisitedMap vm(excluded.begin(), Dune::IdentityMap());
    ParallelInformation pinfo;
    std::size_t aggregates = renumberer.coarsen(pinfo, pg, vm,
                                                *aggregatesMap_, pinfo,
                                                noAggregates);
    std::vector<bool>& visited=excluded;

    typedef std::vector<bool>::iterator Iterator;

    for(Iterator iter= visited.begin(), end=visited.end();
        iter != end; ++iter)
          *iter=false;
    matrix_.reset(productBuilder.build(fineOperator.getmat(), mg, vm,
                                       SequentialInformation(),
                                       *aggregatesMap_,
                                       aggregates,
                                       OverlapFlags()));
    productBuilder.calculate(fineOperator.getmat(), *aggregatesMap_, *matrix_, pinfo, OverlapFlags());
    this->lhs_.resize(this->matrix_->M());
    this->rhs_.resize(this->matrix_->N());
    this->operator_.reset(new O(*matrix_));
  }

  void moveToCoarseLevel(const typename FatherType::FineRangeType& fineRhs)
  {
    Transfer<std::size_t,typename FatherType::FineRangeType,ParallelInformation>
      ::restrictVector(*aggregatesMap_, this->rhs_, fineRhs, ParallelInformation());
    this->lhs_=0;
  }

  void moveToFineLevel(typename FatherType::FineDomainType& fineLhs)
  {
    Transfer<std::size_t,typename FatherType::FineRangeType,ParallelInformation>
      ::prolongateVector(*aggregatesMap_, this->lhs_, fineLhs,
                         prolongDamp_, ParallelInformation());
  }

private:
  typename O::matrix_type::field_type prolongDamp_;
  shared_ptr<AggregatesMap> aggregatesMap_;
  Criterion criterion_;
  shared_ptr<typename O::matrix_type> matrix_;
};

/**
 * @brief A policy class for solving the coarse level system using one step of AMG.
 * @tparam O The type of the linear operator used.
 * @tparam S The type of the smoother used in AMG.
 * @tparam C The type of the crition used for the aggregation within AMG.
 */
template<class O, class S, class C>
class OneStepAMGCoarseSolverPolicy
{
public:
  /** @brief The type of the linear operator used. */
  typedef O Operator;
  /** @brief The type of the range and domain of the operator. */
  typedef typename O::range_type X;
  /** @brief The type of the crition used for the aggregation within AMG.*/
  typedef C Criterion;
  /** @brief The type of the smoother used in AMG. */
  typedef S Smoother;
  /** @brief The type of the arguments used for constructing the smoother. */
  typedef typename Dune::Amg::SmootherTraits<S>::Arguments SmootherArgs;
  /** @brief The type of the AMG construct on the coarse level.*/
  typedef AMG<Operator,X,Smoother> AMGType;
  /**
   * @brief Constructs the coarse solver policy.
   * @param args The arguments used for constructing the smoother.
   * @param c The crition used for the aggregation within AMG.
   */
  OneStepAMGCoarseSolverPolicy(const SmootherArgs& args, const Criterion& c)
    : smootherArgs_(args), criterion_(c)
  {}

private:
  /**
   * @brief A wrapper that makes an inverse operator out of AMG.
   *
   * The operator will use one step of AMG to approximately solve
   * the coarse level system.
   */
  struct AMGInverseOperator : public InverseOperator<X,X>
  {
    AMGInverseOperator(AMGType* amg)
    : amg_(amg), first_(true)
    {}

    void apply(X& x, X& b, double reduction, InverseOperatorResult& res)
    {
      if(first_)
      {
        amg_->pre(x,b);
        first_=false;
        x_.reset(new X(x));
      }
      amg_->apply(x,b);
    }

    void apply(X& x, X& b, InverseOperatorResult& res)
    {
      return apply(x,b,1e-8,res);
    }

    ~AMGInverseOperator()
    {
      if(!first_)
        amg_->post(*x_);
    }
  private:
    shared_ptr<X> x_;
    shared_ptr<AMGType> amg_;
    bool first_;
  };

public:
  template<class P>
  InverseOperator<X,X>* createCoarseLevelSolver(P& transferPolicy)
  {
    coarseOperator_=transferPolicy.getCoarseLevelOperator();
    typedef AMG<O,X,S> AMGType;
    AMGType* amg= new  AMGType(*coarseOperator_,
                           criterion_,
                           smootherArgs_);
    AMGInverseOperator* inv = new AMGInverseOperator(amg);

    return inv; //shared_ptr<InverseOperator<X,X> >(inv);

  }

private:
  shared_ptr<Operator> coarseOperator_;
  SmootherArgs smootherArgs_;
  Criterion criterion_;
};

/**
 * @tparam FO The type of the fine level linear operator.
 * @tparam CO The type of the coarse level linear operator.
 * @tparam S The type of the fine level smoother used.
 */
template<class FO, class CO, class S>
class TwoLevelMethod :
    public Preconditioner<typename FO::domain_type, typename FO::range_type>
{
public:
  /**
   * @brief The linear operator of the finel level system. Has to be
   * derived from AssembledLinearOperator.
   */
  typedef FO FineOperatorType;
  /**
   * @brief The type of the range of the fine level operator.
   */
  typedef typename FineOperatorType::range_type FineRangeType;
  /**
   * @brief The type of the domain of the fine level operator.
   */
  typedef typename FineOperatorType::domain_type FineDomainType;
  /**
   * @brief The linear operator of the finel level system. Has to be
   * derived from AssembledLinearOperator.
   */
  typedef CO CoarseOperatorType;
  /**
   * @brief The type of the range of the coarse level operator.
   */
  typedef typename CoarseOperatorType::range_type CoarseRangeType;
  /**
   * @brief The type of the domain of the coarse level operator.
   */
  typedef typename CoarseOperatorType::domain_type CoarseDomainType;
  /**
   * @brief The type of the fine level smoother.
   */
  typedef S SmootherType;
  // define the category
  enum {
    //! \brief The category the preconditioner is part of.
    category=SolverCategory::sequential
  };

  /**
   * @brief Constructs a two level method.
   *
   * @tparam CoarseSolverPolicy The policy for constructing the coarse
   * solver, e.g. OneStepAMGCoarseSolverPolicy
   * @param op The fine level operator.
   * @param smoother The fine level smoother.
   * @param policy The level transfer policy.
   * @param coarsePolicy The policy for constructing the coarse level solver.
   * @param preSteps The number of smoothing steps to apply before the coarse
   * level correction.
   * @param preSteps The number of smoothing steps to apply after the coarse
   * level correction.
   */
  template<class CoarseSolverPolicy>
  TwoLevelMethod(const FineOperatorType& op,
                 shared_ptr<SmootherType> smoother,
                 shared_ptr<LevelTransferPolicy<FineOperatorType,
                                                CoarseOperatorType> > policy,
                 CoarseSolverPolicy& coarsePolicy,
                 std::size_t preSteps=1, std::size_t postSteps=1)
    : operator_(op), smoother_(smoother), policy_(policy),
      preSteps_(preSteps), postSteps_(postSteps)
  {
    policy_->createCoarseLevelSystem(operator_);
    coarseSolver_.reset(coarsePolicy.createCoarseLevelSolver(*policy_));
  }

  void pre(FineDomainType& x, FineRangeType& b)
  {}

  void post(FineDomainType& x)
  {}

  void apply(FineDomainType& v, const FineRangeType& d)
  {
    FineDomainType u(v);
    FineRangeType rhs(d);
    LevelContext context;
    SequentialInformation info;
    context.pinfo=&info;
    context.lhs=&u;
    context.update=&v;
    context.smoother=smoother_;
    context.rhs=&rhs;
    context.matrix=&operator_;
    // Presmoothing
    presmooth(context, preSteps_);

    //Coarse grid correction
    policy_->moveToCoarseLevel(*context.rhs);
    InverseOperatorResult res;
    coarseSolver_->apply(policy_->getCoarseLevelLhs(), policy_->getCoarseLevelRhs(), res);
    policy_->moveToFineLevel(*context.lhs);

    // Postsmoothing
    presmooth(context, postSteps_);

  }

private:
  /**
   * @brief Struct containing the level information.
   */
  struct LevelContext
  {
    typedef S SmootherType;
    shared_ptr<SmootherType> smoother;
    FineDomainType* lhs;
    FineRangeType* rhs;
    FineDomainType* update;
    SequentialInformation* pinfo;
    const FineOperatorType* matrix;
  };
  const FineOperatorType& operator_;
  /** @brief The coarse level solver. */
  shared_ptr<InverseOperator<typename CO::domain_type,
                             typename CO::range_type> > coarseSolver_;
  /** @brief The fine level smoother. */
  shared_ptr<S> smoother_;
  /** @brief Policy for prolongation, restriction, and coarse level system creation. */
  shared_ptr<LevelTransferPolicy<FO,CO> > policy_;
  /** @brief The number of presmoothing steps to apply. */
  std::size_t preSteps_;
  /** @brief The number of postsmoothing steps to apply. */
  std::size_t postSteps_;
};
}// end namespace Amg
}// end namespace Dune

/** @} */
#endif