/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                                 *
 * ALPS Project: Algorithms and Libraries for Physics Simulations                  *
 *                                                                                 *
 * ALPS Libraries                                                                  *
 *                                                                                 *
 * Copyright (C) 2011 - 2013 by Mario Koenz <mkoenz@ethz.ch>                       *
 *                              Lukas Gamper <gamperl@gmail.com>                   *
 *                                                                                 *
 * This software is part of the ALPS libraries, published under the ALPS           *
 * Library License; you can use, redistribute it and/or modify it under            *
 * the terms of the license, either version 1 or (at your option) any later        *
 * version.                                                                        *
 *                                                                                 *
 * You should have received a copy of the ALPS Library License along with          *
 * the ALPS Libraries; see the file LICENSE.txt. If not, the license is also       *
 * available from http://alps.comp-phys.org/.                                      *
 *                                                                                 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR     *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,        *
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT       *
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE       *
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,     *
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER     *
 * DEALINGS IN THE SOFTWARE.                                                       *
 *                                                                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef ALPS_NGS_ACCUMULATOR_MEAN_HPP
#define ALPS_NGS_ACCUMULATOR_MEAN_HPP

#include <alps/ngs/alea/next/feature.hpp>
#include <alps/ngs/alea/next/feature/count.hpp>

#include <alps/hdf5.hpp>
#include <alps/ngs/numeric.hpp>
#include <alps/ngs/stacktrace.hpp>
#include <alps/ngs/short_print.hpp>

#include <boost/mpl/if.hpp>
#include <boost/utility.hpp>
#include <boost/type_traits/is_scalar.hpp>
#include <boost/type_traits/is_integral.hpp>

#include <stdexcept>

namespace alps {
	namespace accumulator {
		namespace tag {
			struct mean;
		}

		template<typename T> struct mean_type
			: public boost::mpl::if_<boost::is_integral<typename value_type<T>::type>, double, typename value_type<T>::type>
		{};

		template<typename T> struct has_feature<T, tag::mean> {
			template<typename R, typename C> static char helper(R(C::*)() const);
			template<typename C> static char check(boost::integral_constant<std::size_t, sizeof(helper(&C::mean))>*);
			template<typename C> static double check(...);
			typedef boost::integral_constant<bool, sizeof(char) == sizeof(check<T>(0))> type;
		};

		template<typename T> typename mean_type<T>::type mean(T const & arg) {
			return arg.mean();
		}

		namespace detail {

			template<typename A> typename boost::enable_if<
				  typename has_feature<A, tag::mean>::type
				, typename mean_type<A>::type
			>::type mean_impl(A const & acc) {
				return mean(acc);
			}

			template<typename A> typename boost::disable_if<
				  typename has_feature<A, tag::mean>::type
				, typename mean_type<A>::type
			>::type mean_impl(A const & acc) {
				throw std::runtime_error(std::string(typeid(A).name()) + " has no mean-method" + ALPS_STACKTRACE);
				return typename mean_type<A>::type();
			}
		}

		namespace impl {

			template<typename T, typename B> struct Accumulator<T, tag::mean, B> : public B {

				public:
					typedef typename alps::accumulator::mean_type<B>::type mean_type;
					typedef Result<T, tag::mean, typename B::result_type> result_type;

					template<typename ArgumentPack> Accumulator(ArgumentPack const & args): B(args), m_sum(T()) {}

					Accumulator(): B(), m_sum(T()) {}
					Accumulator(Accumulator const & arg): B(arg), m_sum(arg.m_sum) {}

					mean_type const mean() const {
						using alps::ngs::numeric::operator/;

						// TODO: make library for scalar type
						typename alps::hdf5::scalar_type<mean_type>::type cnt = B::count();
						return mean_type(m_sum) / cnt;
					}

					void operator()(T const & val) {
						using alps::ngs::numeric::operator+=;
						using alps::ngs::numeric::detail::check_size;

						B::operator()(val);
						check_size(m_sum, val);
						m_sum += val;
					}

					template<typename S> void print(S & os) const {
						os << alps::short_print(mean());
						B::print(os);
					}

					void save(hdf5::archive & ar) const {
						B::save(ar);
						ar["mean/value"] = mean();
					}

					void load(hdf5::archive & ar) { // TODO: make archive const
						using alps::ngs::numeric::operator*;

						B::load(ar);
						mean_type mean;
						ar["mean/value"] >> mean;
						// TODO: make library for scalar type
						typename alps::hdf5::scalar_type<mean_type>::type cnt = B::count();
						m_sum = mean * cnt;
					}

					static std::size_t rank() { return B::rank() + 1; }
					static bool can_load(hdf5::archive & ar) { // TODO: make archive const
						using alps::hdf5::get_extent;

						return B::can_load(ar)
							&& ar.is_data("mean/value") 
							&& boost::is_scalar<T>::value == ar.is_scalar("mean/value")
							&& (boost::is_scalar<T>::value || get_extent(T()).size() == ar.dimensions("mean/value"))
						;
					}

					void reset() {
						B::reset();
						m_sum = T();
					}

				private:
					T m_sum;
			};

			template<typename T, typename B> class Result<T, tag::mean, B> : public B {

				public:
					typedef typename alps::accumulator::mean_type<B>::type mean_type;

					Result()
						: B()
						, m_mean(mean_type()) 
					{}

					template<typename A> Result(A const & acc)
						: B(acc)
						, m_mean(detail::mean_impl(acc))
					{}

					mean_type const mean() const { 
						return m_mean; 
					}

					template<typename S> void print(S & os) const {
						os << alps::short_print(mean());
						B::print(os);
					}

					void save(hdf5::archive & ar) const {
						B::save(ar);
						ar["mean/value"] = mean();
					}

					void load(hdf5::archive & ar) {
						B::load(ar);
						ar["mean/value"] >> m_mean;
					}

					static std::size_t rank() { return B::rank() + 1; }
					static bool can_load(hdf5::archive & ar) { // TODO: make archive const
						using alps::hdf5::get_extent;

						return B::can_load(ar) 
							&& ar.is_data("mean/value") 
							&& boost::is_scalar<T>::value == ar.is_scalar("mean/value")
							&& (boost::is_scalar<T>::value || get_extent(T()).size() == ar.dimensions("mean/value"))
						;
					}

				private:
					mean_type m_mean;
			};

			template<typename B> class BaseWrapper<tag::mean, B> : public B {
				public:
					virtual bool has_mean() const = 0;
			};

			template<typename T, typename B> class ResultTypeWrapper<T, tag::mean, B> : public B {
				public:
					virtual typename mean_type<B>::type mean() const = 0;
			};

			template<typename T, typename B> class DerivedWrapper<T, tag::mean, B> : public B {
				public:
					DerivedWrapper(): B() {}
					DerivedWrapper(T const & arg): B(arg) {}

					bool has_mean() const { return has_feature<T, tag::mean>::type::value; }

					typename mean_type<B>::type mean() const { return detail::mean_impl(this->m_data); }
			};

		}
	}
}

 #endif