/*****************************************************************************
*
* ALPS Project: Algorithms and Libraries for Physics Simulations
*
* ALPS Libraries
*
* Copyright (C) 1997-2008 by Synge Todo <wistaria@comp-phys.org>
*
* This software is part of the ALPS libraries, published under the ALPS
* Library License; you can use, redistribute it and/or modify it under
* the terms of the license, either version 1 or (at your option) any later
* version.
* 
* You should have received a copy of the ALPS Library License along with
* the ALPS Libraries; see the file LICENSE.txt. If not, the license is also
* available from http://alps.comp-phys.org/.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
* FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT 
* SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE 
* FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, 
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
* DEALINGS IN THE SOFTWARE.
*
*****************************************************************************/

#ifndef PARAPACK_EXCHANGE_MPI_H
#define PARAPACK_EXCHANGE_MPI_H

#include "exchange.h"
#include "parallel.h"
#include "process_mpi.h"

namespace mpi = boost::mpi;

namespace alps {
namespace parapack {

template<typename WALKER, typename INITIALIZER = exmc::no_initializer>
class parallel_exchange_worker : public mc_worker {
private:
  typedef mc_worker super_type;
  typedef WALKER walker_type;
  typedef typename walker_type::weight_parameter_type weight_parameter_type;
  typedef INITIALIZER initializer_type;
  typedef exmc::initializer_helper<walker_type, initializer_type> helper;
  typedef exmc::walker_direc walker_direc;

public:
  static std::string version() { return walker_type::version(); }
  static void print_copyright(std::ostream& out) { walker_type::print_copyright(out); }

  parallel_exchange_worker(mpi::communicator const& comm, alps::Parameters const& params)
    : super_type(params), comm_(comm), init_(params), beta_(params), mcs_(params),
      num_returnee_(0), r_uint_(*engine_ptr, boost::uniform_int<unsigned int>()) {

    int nrep = beta_.size();
    boost::tie(nrep_local_, offset_local_) = calc_nrep(comm_.rank());
    if (nrep_local_ == 0) {
      std::cerr << "Error: number of replicas is smaller than number of processes\n";
      boost::throw_exception(std::runtime_error(
        "number of replicas is smaller than number of processes"));
    }
    if (comm_.rank() == 0) {
      nreps_.resize(comm_.size());
      offsets_.resize(comm_.size());
      nrep_max_ = 0;
      for (int p = 0; p < comm_.size(); ++p) {
        boost::tie(nreps_[p], offsets_[p]) = calc_nrep(p);
        nrep_max_ = std::max(nrep_max_, nreps_[p]);
      }
      std::clog << "EXMC: number of replicas = " << nrep << std::endl
                << "EXMC: number of replicas on each process = "
                << write_vector(nreps_) << std::endl
                << "EXMC: initial inverse temperature set = "
                << write_vector(beta_, " ", 5) << std::endl;
    }

    // initialize walkers
    walker_.resize(nrep_local_);
    tid_local_.resize(nrep_local_);
    alps::Parameters wp(params);
    for (int p = 0; p < nrep_local_; ++p) {
      // different WORKER_SEED for each walker, same DISORDER_SEED for all walkers
      for (int j = 1; j < 3637 /* 509th prime number */; ++j) random_int();
      wp["WORKER_SEED"] = random_int();
      walker_[p] = helper::create_walker(wp, init_);
      tid_local_[p] = p + offset_local_;
    }
    if (comm_.rank() == 0) {
      tid_.resize(nrep);
      for (int p = 0; p < nrep; ++p) tid_[p] = p;
    }

    if (comm_.rank() == 0 && mcs_.exchange()) {
      weight_parameters_.resize(nrep);
      for (int p = 0; p < nrep; ++p) weight_parameters_[p] = weight_parameter_type(0);
    }

    // initialize walker labels
    if (comm_.rank() == 0) {
      wid_.resize(nrep);
      for (int p = 0; p < nrep; ++p) wid_[p] = p;
      if (mcs_.exchange()) {
        direc_.resize(nrep);
        direc_[0] = walker_direc::down;
        for (int p = 1; p < nrep; ++p) direc_[p] = walker_direc::unlabeled;
      }
    }

    // working space
    if (mcs_.exchange()) {
      wp_local_.resize(nrep_local_);
      if (comm_.rank() == 0) {
        wp_.resize(nrep);
        upward_.resize(nrep);
        accept_.resize(nrep - 1);
        if (mcs_.random_exchange()) permutation_.resize(nrep - 1);
      }
    }
  }
  virtual ~parallel_exchange_worker() {}

  void init_observables(alps::Parameters const& params, std::vector<alps::ObservableSet>& obs) {
    int nrep = beta_.size();
    obs.resize(nrep);
    for (int p = nrep_local_; p < nrep; ++p)
      helper::init_observables(walker_[0], params, init_, obs[p]);
    if (comm_.rank() == 0) {
      for (int p = 0; p < nrep; ++p) {
        obs[p] << SimpleRealObservable("EXMC: Temperature")
               << SimpleRealObservable("EXMC: Inverse Temperature");
        if (mcs_.exchange()) {
          obs[p] << SimpleRealObservable("EXMC: Acceptance Rate")
                 << SimpleRealObservable("EXMC: Ratio of Upward-Moving Walker")
                 << SimpleRealObservable("EXMC: Ratio of Downward-Moving Walker");
          obs[p]["EXMC: Acceptance Rate"].reset(true);
          obs[p]["EXMC: Ratio of Upward-Moving Walker"].reset(true);
          obs[p]["EXMC: Ratio of Downward-Moving Walker"].reset(true);
        }
      }
      if (mcs_.exchange()) obs[0] << SimpleRealObservable("EXMC: Inverse Round-Trip Time");
    }
  }

  void run(std::vector<alps::ObservableSet>& obs) {
    ++mcs_;

    int nrep = beta_.size();

    if (comm_.rank() == 0) {
      for (int p = 0; p < nrep; ++p) {
        obs[p]["EXMC: Temperature"] << 1. / beta_[p];
        obs[p]["EXMC: Inverse Temperature"] << beta_[p];
      }
    }

    // MC update of each replica
    for (int w = 0; w < nrep_local_; ++w) {
      int p = tid_local_[w];
      walker_[w]->set_beta(beta_[p]);
      helper::run_walker(walker_[w], init_, obs[p]);
    }

    // replica exchange process
    if (mcs_.exchange() && (mcs_() % mcs_.interval()) == 0) {

      bool continue_stage = false;
      bool next_stage = false;

      for (int w = 0; w < nrep_local_; ++w) wp_local_[w] = walker_[w]->weight_parameter();
      collect_vector(comm_, nreps_, offsets_, wp_local_, wp_);
      if (comm_.rank() == 0) {
        for (int w = 0; w < nrep; ++w) {
          int p = tid_[w];
          weight_parameters_[p] += wp_[w];
        }
      }

      if (comm_.rank() == 0) {
        if (mcs_.random_exchange()) {
          // random exchange
          for (int p = 0; p < nrep - 1; ++p) permutation_[p] = p;
          alps::random_shuffle(permutation_.begin(), permutation_.end(), uniform_01);

          for (int i = 0; i < nrep - 1; ++i) {
            int p = permutation_[i];
            int w0 = wid_[p];
            int w1 = wid_[p+1];
            double logp = ((walker_type::log_weight(wp_[w1], beta_[p]  ) +
                            walker_type::log_weight(wp_[w0], beta_[p+1])) -
                           (walker_type::log_weight(wp_[w1], beta_[p+1]) +
                            walker_type::log_weight(wp_[w0], beta_[p]  )));
            if (logp > 0 || uniform_01() < std::exp(logp)) {
              std::swap(tid_[w0], tid_[w1]);
              std::swap(wid_[p], wid_[p+1]);
              obs[p]["EXMC: Acceptance Rate"] << 1.;
            } else {
              obs[p]["EXMC: Acceptance Rate"] << 0.;
            }
          }
        } else {
          // alternating exchange
          int start = (mcs_() / mcs_.interval()) % 2;
          for (int p = start; p < nrep - 1; p += 2) {
            int w0 = wid_[p];
            int w1 = wid_[p+1];
            double logp = ((walker_type::log_weight(wp_[w1], beta_[p]  ) +
                            walker_type::log_weight(wp_[w0], beta_[p+1])) -
                           (walker_type::log_weight(wp_[w1], beta_[p+1]) +
                            walker_type::log_weight(wp_[w0], beta_[p]  )));
            if (logp > 0 || uniform_01() < std::exp(logp)) {
              std::swap(tid_[w0], tid_[w1]);
              std::swap(wid_[p], wid_[p+1]);
              obs[p]["EXMC: Acceptance Rate"] << 1.;
            } else {
              obs[p]["EXMC: Acceptance Rate"] << 0.;
            }
          }
        }

        if (direc_[wid_.front()] == walker_direc::up) {
          obs[0]["EXMC: Inverse Round-Trip Time"] << 1. / nrep;
          ++num_returnee_;
        } else {
          obs[0]["EXMC: Inverse Round-Trip Time"] << 0.;
        }
        direc_[wid_.front()] = walker_direc::down;
        if (direc_[wid_.back()] == walker_direc::down) direc_[wid_.back()] = walker_direc::up;
        for (int p = 0; p < nrep; ++p) {
          obs[p]["EXMC: Ratio of Upward-Moving Walker"] <<
            (direc_[wid_[p]] == walker_direc::up ? 1. : 0.);
          obs[p]["EXMC: Ratio of Downward-Moving Walker"] <<
            (direc_[wid_[p]] == walker_direc::down ? 1. : 0.);
        }

        if (mcs_.doing_optimization() && mcs_.stage_count() == mcs_.stage_sweeps()) {

          if (mcs_.optimization_type() == exmc::exchange_steps::rate) {

            for (int p = 0; p < nrep - 1; ++p)
              accept_[p] =
                reinterpret_cast<SimpleRealObservable&>(obs[p]["EXMC: Acceptance Rate"]).mean();
            for (int p = 0; p < nrep; ++p) wp_[p] = weight_parameters_[p] / mcs_.stage_count();
            std::clog << "EXMC stage " << mcs_.stage() << ": acceptance rate = "
                      << write_vector(accept_, " ", 5) << std::endl;

            if (mcs_.stage() != 0) {
              beta_.optimize_h1999(wp_);
              std::clog << "EXMC stage " << mcs_.stage() << ": optimized inverse temperature set = "
                        << write_vector(beta_, " ", 5) << std::endl;
            }
            next_stage = true;

            for (int p = 0; p < nrep; ++p) {
              obs[p]["EXMC: Acceptance Rate"].reset(true);
              obs[p]["EXMC: Ratio of Upward-Moving Walker"].reset(true);
              obs[p]["EXMC: Ratio of Downward-Moving Walker"].reset(true);
              weight_parameters_[p] = weight_parameter_type(0);
            }

          } else {

            bool success = (num_returnee_ >= nrep);

            int nu = 0;
            for (int p = 0; p < nrep; ++p) if (direc_[p] == walker_direc::unlabeled) ++nu;
            if (nu > 0) success = false;

            for (int p = 0; p < nrep; ++p) {
              double up = reinterpret_cast<SimpleRealObservable&>(
                obs[p]["EXMC: Ratio of Upward-Moving Walker"]).mean();
              double down = reinterpret_cast<SimpleRealObservable&>(
                obs[p]["EXMC: Ratio of Downward-Moving Walker"]).mean();
              upward_[p] = (up + down > 0) ? up / (up + down) : alps::nan();
            }

            for (int p = 0; p < nrep - 1; ++p)
              accept_[p] = reinterpret_cast<SimpleRealObservable&>(
                obs[p]["EXMC: Acceptance Rate"]).mean();

            std::clog << "EXMC stage " << mcs_.stage()
                      << ": stage count = " << mcs_.stage_count() << '\n'
                      << "EXMC stage " << mcs_.stage()
                      << ": number of returned walkers = " << num_returnee_ << '\n'
                      << "EXMC stage " << mcs_.stage()
                      << ": number of unlabeled walkers = " << nu << '\n'
                      << "EXMC stage " << mcs_.stage()
                      << ": population ratio of upward-moving walkers "
                      << write_vector(upward_, " ", 5) << '\n'
                      << "EXMC stage " << mcs_.stage()
                      << ": acceptance rate " << write_vector(accept_, " ", 5) << std::endl;

            // preform optimization
            if (mcs_.stage() != 0 && success) success = beta_.optimize2(upward_);

            if (success) {
              std::clog << "EXMC stage " << mcs_.stage() << ": DONE" << std::endl;
              if (mcs_.stage() > 0)
                std::clog << "EXMC stage " << mcs_.stage() << ": optimized inverse temperature set = "
                          << write_vector(beta_, " ", 5) << std::endl;
              next_stage = true;
              for (int p = 0; p < nrep; ++p) {
                obs[p]["EXMC: Acceptance Rate"].reset(true);
                obs[p]["EXMC: Ratio of Upward-Moving Walker"].reset(true);
                obs[p]["EXMC: Ratio of Downward-Moving Walker"].reset(true);
              }
              num_returnee_ = 0;
            } else {
              // increase stage sweeps
              continue_stage = true;
              std::clog << "EXMC stage " << mcs_.stage() << ": NOT FINISHED\n"
                        << "EXMC stage " << mcs_.stage() << ": increased number of sweeps to "
                        << mcs_.stage_sweeps() << std::endl;
            }
          }

          // check whether all the replicas have revisited the highest temperature or not
          if (!mcs_.perform_optimization() && mcs_() == mcs_.thermalization()) {
            int nu = 0;
            for (int p = 0; p < nrep; ++p) if (direc_[p] == walker_direc::unlabeled) ++nu;
            std::clog << "EXMC: thermalization count = " << mcs_() << '\n'
                      << "EXMC: number of returned walkers = " << num_returnee_ << '\n'
                      << "EXMC: number of unlabeled walkers = " << nu << std::endl;
            if ((num_returnee_ >= nrep) && (nu == 0)) {
              std::clog << "EXMC: thermzlization DONE" << std::endl;
            } else {
              continue_stage = true;
              std::clog << "EXMC: thermalization NOT FINISHED\n"
                        << "EXMC: increased number of thermalization sweeps to "
                        << mcs_.thermalization() << std::endl;
            }
          }
        }
      }

      // broadcast EXMC results
      broadcast(comm_, continue_stage, 0);
      broadcast(comm_, next_stage, 0);
      if (continue_stage) mcs_.continue_stage();
      if (next_stage) mcs_.next_stage();
      distribute_vector(comm_, nreps_, offsets_, tid_, tid_local_);
    }
  }

  void save(alps::ODump& dp) const {
    dp << beta_ << mcs_ << tid_local_;
    if (comm_.rank() == 0) dp << tid_ << wid_ << direc_ << num_returnee_ << weight_parameters_;
    for (int i = 0; i < nrep_local_; ++i) walker_[i]->save(dp);
  }
  void load(alps::IDump& dp) {
    dp >> beta_ >> mcs_ >> tid_local_;
    if (comm_.rank() == 0) dp >> tid_ >> wid_ >> direc_ >> num_returnee_ >> weight_parameters_;
    for (int i = 0; i < nrep_local_; ++i) walker_[i]->load(dp);
  }

  bool is_thermalized() const { return mcs_.is_thermalized(); }
  double progress() const { return mcs_.progress(); }

  static void evaluate_observable(alps::ObservableSet& obs) {
    walker_type::evaluate_observable(obs);
  }

protected:
  std::pair<int, int> calc_nrep(int id) const {
    int nrep = beta_.size();
    int n = nrep / comm_.size();
    int f;
    if (id < nrep - n * comm_.size()) {
      ++n;
      f = n * id;
    } else {
      f = (nrep - n * comm_.size()) + n * id;
    }
    return std::make_pair(n, f);
  }

private:
  mpi::communicator comm_;

  int nrep_local_;           // number of walkers (replicas) on this process
  int offset_local_;         // first (global) id of walker on this process
  int nrep_max_;             // [master only] maximum number of walkers (replicas) on a process
  std::vector<int> nreps_;   // [master only] number of walkers (replicas) on each process
  std::vector<int> offsets_; // [master only] first (global) id of walker on each process

  initializer_type init_;
  std::vector<boost::shared_ptr<walker_type> > walker_; // [0..nrep_local_)

  exmc::inverse_temperature_set<walker_type> beta_;
  exmc::exchange_steps mcs_;
  std::vector<int> tid_local_; // temperature id of each walker (replica)
  std::vector<int> tid_;       // [master only] temperature id of each walker (replica)
  std::vector<int> wid_;       // [master only] walker (replica) id at each temperature
  std::vector<int> direc_;     // [master only] direction of each walker (replica)
  int num_returnee_;           // [master only] number of walkers returned to highest temperature
  std::vector<weight_parameter_type> weight_parameters_; // [master only]

  boost::variate_generator<engine_type&, boost::uniform_int<unsigned int> > r_uint_;

  // working space
  std::vector<weight_parameter_type> wp_local_;
  std::vector<weight_parameter_type> wp_; // [master only]
  std::vector<double> upward_;            // [master only]
  std::vector<double> accept_;            // [master only]
  std::vector<int> permutation_;          // [master only]
};

} // end namespace parapack
} // end namespace alps

#endif // PARAPACK_EXCHANGE_H