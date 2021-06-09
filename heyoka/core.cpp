// Copyright 2020, 2021 Francesco Biscani (bluescarni@gmail.com), Dario Izzo (dario.izzo@gmail.com)
//
// This file is part of the heyoka.py library.
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <heyoka/config.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <boost/numeric/conversion/cast.hpp>

#include <fmt/format.h>

#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <Python.h>

#if defined(HEYOKA_HAVE_REAL128)

#include <mp++/extra/pybind11.hpp>
#include <mp++/real128.hpp>

#endif

#include <heyoka/exceptions.hpp>
#include <heyoka/expression.hpp>
#include <heyoka/llvm_state.hpp>
#include <heyoka/mascon.hpp>
#include <heyoka/math.hpp>
#include <heyoka/nbody.hpp>
#include <heyoka/number.hpp>
#include <heyoka/taylor.hpp>

#include "common_utils.hpp"
#include "logging.hpp"
#include "long_double_caster.hpp"
#include "setup_networkx.hpp"
#include "setup_sympy.hpp"
#include "taylor_add_jet.hpp"
#include "taylor_expose_events.hpp"
#include "taylor_expose_integrator.hpp"

namespace py = pybind11;
namespace hey = heyoka;
namespace heypy = heyoka_py;

#if defined(__clang__)

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"

#endif

PYBIND11_MODULE(core, m)
{
#if defined(HEYOKA_HAVE_REAL128)
    // Init the pybind11 integration for this module.
    mppp_pybind11::init();
#endif

    using namespace pybind11::literals;
    using fmt::literals::operator""_format;
    namespace kw = hey::kw;

    m.doc() = "The core heyoka module";

    // Flag the presence of real128.
    m.attr("with_real128") =
#if defined(HEYOKA_HAVE_REAL128)
        true
#else
        false
#endif
        ;

    // Connect heyoka's logging to Python's logging.
    heypy::enable_logging();

    // Expose the logging setter functions.
    heypy::expose_logging_setters(m);

    // Expose testing functions.
    m.def("_test_debug_msg", &heypy::test_debug_msg);
    m.def("_test_info_msg", &heypy::test_info_msg);
    m.def("_test_warning_msg", &heypy::test_warning_msg);
    m.def("_test_error_msg", &heypy::test_error_msg);
    m.def("_test_critical_msg", &heypy::test_critical_msg);

#if defined(HEYOKA_HAVE_REAL128)
    if (!heypy::mpmath_available()) {
        py::module_::import("warnings")
            .attr("warn")("The heyoka C++ library was compiled with quadruple-precision support, but the 'mpmath' "
                          "Python module is not installed");
    }
#endif

    // Export the heyoka version.
    m.attr("_heyoka_cpp_version_major") = HEYOKA_VERSION_MAJOR;
    m.attr("_heyoka_cpp_version_minor") = HEYOKA_VERSION_MINOR;
    m.attr("_heyoka_cpp_version_patch") = HEYOKA_VERSION_PATCH;

    // Register heyoka's custom exceptions.
    py::register_exception_translator([](std::exception_ptr p) {
        try {
            if (p) {
                std::rethrow_exception(p);
            }
        } catch (const hey::not_implemented_error &nie) {
            PyErr_SetString(PyExc_NotImplementedError, nie.what());
        }
    });

    py::register_exception_translator([](std::exception_ptr p) {
        try {
            if (p) {
                std::rethrow_exception(p);
            }
        } catch (const hey::zero_division_error &zde) {
            PyErr_SetString(PyExc_ZeroDivisionError, zde.what());
        }
    });

    // NOTE: typedef to avoid complications in the
    // exposition of the operators.
    using ld_t = long double;

    py::class_<hey::expression>(m, "expression")
        .def(py::init<>())
        .def(py::init<double>())
        .def(py::init<long double>())
#if defined(HEYOKA_HAVE_REAL128)
        .def(py::init<mppp::real128>())
#endif
        .def(py::init<std::string>())
        // Unary operators.
        .def(-py::self)
        .def(+py::self)
        // Binary operators.
        .def(py::self + py::self)
        .def(py::self + double())
        .def(double() + py::self)
        .def(py::self + ld_t())
        .def(ld_t() + py::self)
#if defined(HEYOKA_HAVE_REAL128)
        .def(py::self + mppp::real128())
        .def(mppp::real128() + py::self)
#endif
        .def(py::self - py::self)
        .def(py::self - double())
        .def(double() - py::self)
        .def(py::self - ld_t())
        .def(ld_t() - py::self)
#if defined(HEYOKA_HAVE_REAL128)
        .def(py::self - mppp::real128())
        .def(mppp::real128() - py::self)
#endif
        .def(py::self * py::self)
        .def(py::self * double())
        .def(double() * py::self)
        .def(py::self * ld_t())
        .def(ld_t() * py::self)
#if defined(HEYOKA_HAVE_REAL128)
        .def(py::self * mppp::real128())
        .def(mppp::real128() * py::self)
#endif
        .def(py::self / py::self)
        .def(py::self / double())
        .def(double() / py::self)
        .def(py::self / ld_t())
        .def(ld_t() / py::self)
#if defined(HEYOKA_HAVE_REAL128)
        .def(py::self / mppp::real128())
        .def(mppp::real128() / py::self)
#endif
        // In-place operators.
        .def(py::self += py::self)
        .def(py::self += double())
        .def(py::self += ld_t())
#if defined(HEYOKA_HAVE_REAL128)
        .def(py::self += mppp::real128())
#endif
        .def(py::self -= py::self)
        .def(py::self -= double())
        .def(py::self -= ld_t())
#if defined(HEYOKA_HAVE_REAL128)
        .def(py::self -= mppp::real128())
#endif
        .def(py::self *= py::self)
        .def(py::self *= double())
        .def(py::self *= ld_t())
#if defined(HEYOKA_HAVE_REAL128)
        .def(py::self *= mppp::real128())
#endif
        .def(py::self /= py::self)
        .def(py::self /= double())
        .def(py::self /= ld_t())
#if defined(HEYOKA_HAVE_REAL128)
        .def(py::self /= mppp::real128())
#endif
        // Comparisons.
        .def(py::self == py::self)
        .def(py::self != py::self)
        // pow().
        .def("__pow__", [](const hey::expression &b, const hey::expression &e) { return hey::pow(b, e); })
        .def("__pow__", [](const hey::expression &b, double e) { return hey::pow(b, e); })
        .def("__pow__", [](const hey::expression &b, long double e) { return hey::pow(b, e); })
#if defined(HEYOKA_HAVE_REAL128)
        .def("__pow__", [](const hey::expression &b, mppp::real128 e) { return hey::pow(b, e); })
#endif
        // Repr.
        .def("__repr__",
             [](const hey::expression &e) {
                 std::ostringstream oss;
                 oss << e;
                 return oss.str();
             })
        // Copy/deepcopy.
        .def("__copy__", [](const hey::expression &e) { return e; })
        .def(
            "__deepcopy__", [](const hey::expression &e, py::dict) { return e; }, "memo"_a);

    // Eval
    m.def("_eval_dbl", [](const hey::expression &e, const std::unordered_map<std::string, double> &map,
                          const std::vector<double> &pars) { return hey::eval<double>(e, map, pars); });
    m.def("_eval_ldbl", [](const hey::expression &e, const std::unordered_map<std::string, long double> &map,
                           const std::vector<long double> &pars) { return hey::eval<long double>(e, map, pars); });
#if defined(HEYOKA_HAVE_REAL128)
    m.def("_eval_f128", [](const hey::expression &e, const std::unordered_map<std::string, mppp::real128> &map,
                           const std::vector<mppp::real128> &pars) { return hey::eval<mppp::real128>(e, map, pars); });
#endif

    // Pairwise sum/prod.
    m.def("pairwise_sum", [](std::vector<hey::expression> v_ex) { return hey::pairwise_sum(std::move(v_ex)); });
    m.def("pairwise_prod", [](std::vector<hey::expression> v_ex) { return hey::pairwise_prod(std::move(v_ex)); });

    // make_vars() helper.
    m.def("make_vars", [](py::args v_str) {
        py::list retval;
        for (auto o : v_str) {
            retval.append(hey::expression(py::cast<std::string>(o)));
        }
        return retval;
    });

    // Math functions.
    m.def("square", &hey::square);
    m.def("sqrt", &hey::sqrt);
    m.def("log", &hey::log);
    m.def("exp", &hey::exp);
    m.def("sin", &hey::sin);
    m.def("cos", &hey::cos);
    m.def("tan", &hey::tan);
    m.def("asin", &hey::asin);
    m.def("acos", &hey::acos);
    m.def("atan", &hey::atan);
    m.def("sinh", &hey::sinh);
    m.def("cosh", &hey::cosh);
    m.def("tanh", &hey::tanh);
    m.def("asinh", &hey::asinh);
    m.def("acosh", &hey::acosh);
    m.def("atanh", &hey::atanh);
    m.def("sigmoid", &hey::sigmoid);
    m.def("erf", &hey::erf);
    m.def("powi", &hey::powi);

    // kepE().
    m.def("kepE", [](hey::expression e, hey::expression M) { return hey::kepE(std::move(e), std::move(M)); });

    m.def("kepE", [](double e, hey::expression M) { return hey::kepE(e, std::move(M)); });
    m.def("kepE", [](long double e, hey::expression M) { return hey::kepE(e, std::move(M)); });
#if defined(HEYOKA_HAVE_REAL128)
    m.def("kepE", [](mppp::real128 e, hey::expression M) { return hey::kepE(e, std::move(M)); });
#endif

    m.def("kepE", [](hey::expression e, double M) { return hey::kepE(std::move(e), M); });
    m.def("kepE", [](hey::expression e, long double M) { return hey::kepE(std::move(e), M); });
#if defined(HEYOKA_HAVE_REAL128)
    m.def("kepE", [](hey::expression e, mppp::real128 M) { return hey::kepE(std::move(e), M); });
#endif

    // Time.
    m.attr("time") = hey::time;

    // tpoly().
    m.def("tpoly", &hey::tpoly);

    // Diff.
    m.def("diff", [](const hey::expression &ex, const std::string &s) { return hey::diff(ex, s); });
    m.def("diff", [](const hey::expression &ex, const hey::expression &var) { return hey::diff(ex, var); });

    // Syntax sugar for creating parameters.
    py::class_<hey::detail::par_impl>(m, "_par_generator").def("__getitem__", &hey::detail::par_impl::operator[]);
    m.attr("par") = hey::detail::par_impl{};

    // N-body builders.
    m.def(
        "make_nbody_sys",
        [](std::uint32_t n, py::object Gconst, std::optional<py::iterable> masses) {
            const auto G = heypy::to_number(Gconst);

            std::vector<hey::number> m_vec;
            if (masses) {
                for (auto ms : *masses) {
                    m_vec.push_back(heypy::to_number(ms));
                }
            } else {
                // If masses are not provided, all masses are 1.
                m_vec.resize(static_cast<decltype(m_vec.size())>(n), hey::number{1.});
            }

            return hey::make_nbody_sys(n, kw::Gconst = G, kw::masses = m_vec);
        },
        "n"_a, "Gconst"_a = 1., "masses"_a = py::none{});

    m.def(
        "make_nbody_par_sys",
        [](std::uint32_t n, py::object Gconst, std::optional<std::uint32_t> n_massive) {
            const auto G = heypy::to_number(Gconst);

            if (n_massive) {
                return hey::make_nbody_par_sys(n, kw::Gconst = G, kw::n_massive = *n_massive);
            } else {
                return hey::make_nbody_par_sys(n, kw::Gconst = G);
            }
        },
        "n"_a, "Gconst"_a = 1., "n_massive"_a = py::none{});

    // mascon dynamics builder
    m.def(
        "make_mascon_system",
        [](py::object Gconst, py::iterable points, py::iterable masses, py::iterable omega) {
            const auto G = heypy::to_number(Gconst);

            std::vector<std::vector<hey::expression>> points_vec;
            for (auto p : points) {
                std::vector<hey::expression> tmp;
                for (auto el : py::cast<py::iterable>(p)) {
                    tmp.emplace_back(heypy::to_number(el));
                }
                points_vec.emplace_back(tmp);
            }

            std::vector<hey::expression> mass_vec;
            for (auto ms : masses) {
                mass_vec.emplace_back(heypy::to_number(ms));
            }
            std::vector<hey::expression> omega_vec;
            for (auto w : omega) {
                omega_vec.emplace_back(heypy::to_number(w));
            }

            return hey::make_mascon_system(kw::Gconst = G, kw::points = points_vec, kw::masses = mass_vec,
                                           kw::omega = omega_vec);
        },
        "Gconst"_a, "points"_a, "masses"_a, "omega"_a);

    m.def(
        "energy_mascon_system",
        [](py::object Gconst, py::iterable state, py::iterable points, py::iterable masses, py::iterable omega) {
            const auto G = heypy::to_number(Gconst);

            std::vector<hey::expression> state_vec;
            for (auto s : state) {
                state_vec.emplace_back(heypy::to_number(s));
            }

            std::vector<std::vector<hey::expression>> points_vec;
            for (auto p : points) {
                std::vector<hey::expression> tmp;
                for (auto el : py::cast<py::iterable>(p)) {
                    tmp.emplace_back(heypy::to_number(el));
                }
                points_vec.emplace_back(tmp);
            }

            std::vector<hey::expression> mass_vec;
            for (auto ms : masses) {
                mass_vec.emplace_back(heypy::to_number(ms));
            }

            std::vector<hey::expression> omega_vec;
            for (auto w : omega) {
                omega_vec.emplace_back(heypy::to_number(w));
            }

            return hey::energy_mascon_system(kw::Gconst = G, kw::state = state_vec, kw::points = points_vec,
                                             kw::masses = mass_vec, kw::omega = omega_vec);
        },
        "Gconst"_a, "state"_a, "points"_a, "masses"_a, "omega"_a);

    // taylor_outcome enum.
    py::enum_<hey::taylor_outcome>(m, "taylor_outcome", py::arithmetic())
        .value("success", hey::taylor_outcome::success)
        .value("step_limit", hey::taylor_outcome::step_limit)
        .value("time_limit", hey::taylor_outcome::time_limit)
        .value("err_nf_state", hey::taylor_outcome::err_nf_state)
        .value("cb_stop", hey::taylor_outcome::cb_stop);

    // event_direction enum.
    py::enum_<hey::event_direction>(m, "event_direction")
        .value("any", hey::event_direction::any)
        .value("positive", hey::event_direction::positive)
        .value("negative", hey::event_direction::negative);

    // Computation of the jet of derivatives.
    heypy::expose_taylor_add_jet_dbl(m);
    heypy::expose_taylor_add_jet_ldbl(m);

#if defined(HEYOKA_HAVE_REAL128)

    if (heypy::mpmath_available()) {
        heypy::expose_taylor_add_jet_f128(m);
    }

#endif

    // Scalar adaptive taylor integrators.
    heypy::expose_taylor_integrator_dbl(m);
    heypy::expose_taylor_integrator_ldbl(m);

#if defined(HEYOKA_HAVE_REAL128)

    if (heypy::mpmath_available()) {
        heypy::expose_taylor_integrator_f128(m);
    }

#endif

    // Expose the events.
    heypy::expose_taylor_t_event_dbl(m);
    heypy::expose_taylor_t_event_ldbl(m);

#if defined(HEYOKA_HAVE_REAL128)

    if (heypy::mpmath_available()) {
        heypy::expose_taylor_t_event_f128(m);
    }

#endif

    heypy::expose_taylor_nt_event_dbl(m);
    heypy::expose_taylor_nt_event_ldbl(m);

#if defined(HEYOKA_HAVE_REAL128)

    if (heypy::mpmath_available()) {
        heypy::expose_taylor_nt_event_f128(m);
    }

#endif

    // LLVM state.
    py::class_<hey::llvm_state>(m, "llvm_state")
        .def("get_ir", &hey::llvm_state::get_ir)
        .def("get_object_code", [](hey::llvm_state &s) { return py::bytes(s.get_object_code()); })
        // Repr.
        .def("__repr__",
             [](const hey::llvm_state &s) {
                 std::ostringstream oss;
                 oss << s;
                 return oss.str();
             })
        // Copy/deepcopy.
        .def("__copy__", [](const hey::llvm_state &s) { return s; })
        .def(
            "__deepcopy__", [](const hey::llvm_state &s, py::dict) { return s; }, "memo"_a);

    // The callback for the propagate_*() functions for
    // the batch integrator.
    using prop_cb_t = std::function<bool(hey::taylor_adaptive_batch<double> &)>;

    // Batch adaptive integrator for double.
    auto tabd_ctor_impl = [](auto sys, py::array_t<double> state_, std::optional<py::array_t<double>> time_,
                             std::optional<py::array_t<double>> pars_, double tol, bool high_accuracy,
                             bool compact_mode) {
        // Convert state and pars to std::vector, after checking
        // dimensions and shape.
        if (state_.ndim() != 2) {
            heypy::py_throw(PyExc_ValueError,
                            "Invalid state vector passed to the constructor of a batch integrator: "
                            "the expected number of dimensions is 2, but the input array has a dimension of {}"_format(
                                state_.ndim())
                                .c_str());
        }

        // Infer the batch size from the second dimension.
        const auto batch_size = boost::numeric_cast<std::uint32_t>(state_.shape(1));

        // Flatten out and convert to a C++ vector.
        auto state = py::cast<std::vector<double>>(state_.attr("flatten")());

        // If pars is none, an empty vector will be fine.
        std::vector<double> pars;
        if (pars_) {
            auto &pars_arr = *pars_;

            if (pars_arr.ndim() != 2 || boost::numeric_cast<std::uint32_t>(pars_arr.shape(1)) != batch_size) {
                heypy::py_throw(PyExc_ValueError,
                                "Invalid parameter vector passed to the constructor of a batch integrator: "
                                "the expected array shape is (n, {}), but the input array has either the wrong "
                                "number of dimensions or the wrong shape"_format(batch_size)
                                    .c_str());
            }
            pars = py::cast<std::vector<double>>(pars_arr.attr("flatten")());
        }

        if (time_) {
            // Times provided.
            auto &time_arr = *time_;
            if (time_arr.ndim() != 1 || boost::numeric_cast<std::uint32_t>(time_arr.shape(0)) != batch_size) {
                heypy::py_throw(PyExc_ValueError,
                                "Invalid time vector passed to the constructor of a batch integrator: "
                                "the expected array shape is ({}), but the input array has either the wrong "
                                "number of dimensions or the wrong shape"_format(batch_size)
                                    .c_str());
            }
            auto time = py::cast<std::vector<double>>(time_arr);

            py::gil_scoped_release release;

            return hey::taylor_adaptive_batch<double>{std::move(sys),
                                                      std::move(state),
                                                      batch_size,
                                                      kw::time = std::move(time),
                                                      kw::tol = tol,
                                                      kw::high_accuracy = high_accuracy,
                                                      kw::compact_mode = compact_mode,
                                                      kw::pars = std::move(pars)};
        } else {
            // Times not provided.
            py::gil_scoped_release release;

            return hey::taylor_adaptive_batch<double>{std::move(sys),
                                                      std::move(state),
                                                      batch_size,
                                                      kw::tol = tol,
                                                      kw::high_accuracy = high_accuracy,
                                                      kw::compact_mode = compact_mode,
                                                      kw::pars = std::move(pars)};
        }
    };
    py::class_<hey::taylor_adaptive_batch<double>> tabd_c(m, "_taylor_adaptive_batch_dbl");
    tabd_c
        .def(py::init([tabd_ctor_impl](std::vector<std::pair<hey::expression, hey::expression>> sys,
                                       py::array_t<double> state, std::optional<py::array_t<double>> time,
                                       std::optional<py::array_t<double>> pars, double tol, bool high_accuracy,
                                       bool compact_mode) {
                 return tabd_ctor_impl(std::move(sys), state, std::move(time), std::move(pars), tol, high_accuracy,
                                       compact_mode);
             }),
             "sys"_a, "state"_a, "time"_a = py::none{}, "pars"_a = py::none{}, "tol"_a = 0., "high_accuracy"_a = false,
             "compact_mode"_a = false)
        .def(py::init([tabd_ctor_impl](std::vector<hey::expression> sys, py::array_t<double> state,
                                       std::optional<py::array_t<double>> time, std::optional<py::array_t<double>> pars,
                                       double tol, bool high_accuracy, bool compact_mode) {
                 return tabd_ctor_impl(std::move(sys), state, std::move(time), std::move(pars), tol, high_accuracy,
                                       compact_mode);
             }),
             "sys"_a, "state"_a, "time"_a = py::none{}, "pars"_a = py::none{}, "tol"_a = 0., "high_accuracy"_a = false,
             "compact_mode"_a = false)
        .def("get_decomposition", &hey::taylor_adaptive_batch<double>::get_decomposition)
        .def(
            "step", [](hey::taylor_adaptive_batch<double> &ta, bool wtc) { ta.step(wtc); }, "write_tc"_a = false)
        .def(
            "step",
            [](hey::taylor_adaptive_batch<double> &ta, const std::vector<double> &max_delta_t, bool wtc) {
                ta.step(max_delta_t, wtc);
            },
            "max_delta_t"_a, "write_tc"_a = false)
        .def("step_backward", &hey::taylor_adaptive_batch<double>::step_backward, "write_tc"_a = false)
        .def_property_readonly("step_res",
                               [](const hey::taylor_adaptive_batch<double> &ta) { return ta.get_step_res(); })
        .def(
            "propagate_for",
            [](hey::taylor_adaptive_batch<double> &ta, const std::vector<double> &delta_t, std::size_t max_steps,
               const std::vector<double> &max_delta_t, prop_cb_t cb_, bool write_tc) {
                // Create the callback wrapper.
                auto cb = heypy::make_prop_cb(cb_);

                // NOTE: after releasing the GIL here, the only potential
                // calls into the Python interpreter are when invoking cb
                // or the events' callbacks (which are all protected by GIL reacquire).
                // Note that copying cb around or destroying it is harmless, as it contains only
                // a reference to the original callback cb_, or it is an empty callback.
                py::gil_scoped_release release;
                ta.propagate_for(delta_t, kw::max_steps = max_steps, kw::max_delta_t = max_delta_t, kw::callback = cb,
                                 kw::write_tc = write_tc);
            },
            "delta_t"_a, "max_steps"_a = 0, "max_delta_t"_a = std::vector<double>{}, "callback"_a = prop_cb_t{},
            "write_tc"_a = false)
        .def(
            "propagate_until",
            [](hey::taylor_adaptive_batch<double> &ta, const std::vector<double> &t, std::size_t max_steps,
               const std::vector<double> &max_delta_t, prop_cb_t cb_, bool write_tc) {
                // Create the callback wrapper.
                auto cb = heypy::make_prop_cb(cb_);

                py::gil_scoped_release release;
                ta.propagate_until(t, kw::max_steps = max_steps, kw::max_delta_t = max_delta_t, kw::callback = cb,
                                   kw::write_tc = write_tc);
            },
            "t"_a, "max_steps"_a = 0, "max_delta_t"_a = std::vector<double>{}, "callback"_a = prop_cb_t{},
            "write_tc"_a = false)
        .def(
            "propagate_grid",
            [](hey::taylor_adaptive_batch<double> &ta, py::array_t<double> grid, std::size_t max_steps,
               const std::vector<double> &max_delta_t, prop_cb_t cb_) {
                // Check the grid dimension/shape.
                if (grid.ndim() != 2) {
                    heypy::py_throw(
                        PyExc_ValueError,
                        "Invalid grid passed to the propagate_grid() method of a batch integrator: "
                        "the expected number of dimensions is 2, but the input array has a dimension of {}"_format(
                            grid.ndim())
                            .c_str());
                }
                if (boost::numeric_cast<std::uint32_t>(grid.shape(1)) != ta.get_batch_size()) {
                    heypy::py_throw(PyExc_ValueError,
                                    "Invalid grid passed to the propagate_grid() method of a batch integrator: "
                                    "the shape must be (n, {}) but the number of columns is {} instead"_format(
                                        ta.get_batch_size(), grid.shape(1))
                                        .c_str());
                }

                // Convert to a std::vector.
                const auto grid_v = py::cast<std::vector<double>>(grid.attr("flatten")());

                // Create the callback wrapper.
                auto cb = heypy::make_prop_cb(cb_);

                // Run the propagation.
                // NOTE: for batch integrators, ret is guaranteed to always have
                // the same size regardless of errors.
                decltype(ta.propagate_grid(grid_v, max_steps)) ret;
                {
                    py::gil_scoped_release release;
                    ret = ta.propagate_grid(grid_v, kw::max_steps = max_steps, kw::max_delta_t = max_delta_t,
                                            kw::callback = cb);
                }

                // Create the output array.
                assert(ret.size() == grid_v.size() * ta.get_dim());
                py::array_t<double> a_ret(py::array::ShapeContainer{grid.shape(0),
                                                                    boost::numeric_cast<py::ssize_t>(ta.get_dim()),
                                                                    grid.shape(1)},
                                          ret.data());

                return a_ret;
            },
            "grid"_a, "max_steps"_a = 0, "max_delta_t"_a = std::vector<double>{}, "callback"_a = prop_cb_t{})
        .def_property_readonly("propagate_res",
                               [](const hey::taylor_adaptive_batch<double> &ta) { return ta.get_propagate_res(); })
        .def_property_readonly("time",
                               [](py::object &o) {
                                   auto *ta = py::cast<hey::taylor_adaptive_batch<double> *>(o);
                                   py::array_t<double> ret(py::array::ShapeContainer{boost::numeric_cast<py::ssize_t>(
                                                               ta->get_time().size())},
                                                           ta->get_time_data(), o);

                                   // Ensure the returned array is read-only.
                                   ret.attr("flags").attr("writeable") = false;

                                   return ret;
                               })
        .def("set_time", &hey::taylor_adaptive_batch<double>::set_time)
        .def_property_readonly(
            "state",
            [](py::object &o) {
                auto *ta = py::cast<hey::taylor_adaptive_batch<double> *>(o);

                assert(ta->get_state().size() % ta->get_batch_size() == 0u);
                const auto nvars = boost::numeric_cast<py::ssize_t>(ta->get_dim());
                const auto bs = boost::numeric_cast<py::ssize_t>(ta->get_batch_size());

                return py::array_t<double>(py::array::ShapeContainer{nvars, bs}, ta->get_state_data(), o);
            })
        .def_property_readonly(
            "pars",
            [](py::object &o) {
                auto *ta = py::cast<hey::taylor_adaptive_batch<double> *>(o);

                assert(ta->get_pars().size() % ta->get_batch_size() == 0u);
                const auto npars = boost::numeric_cast<py::ssize_t>(ta->get_pars().size() / ta->get_batch_size());
                const auto bs = boost::numeric_cast<py::ssize_t>(ta->get_batch_size());

                return py::array_t<double>(py::array::ShapeContainer{npars, bs}, ta->get_pars_data(), o);
            })
        .def_property_readonly(
            "tc",
            [](const py::object &o) {
                auto *ta = py::cast<const hey::taylor_adaptive_batch<double> *>(o);

                const auto nvars = boost::numeric_cast<py::ssize_t>(ta->get_dim());
                const auto ncoeff = boost::numeric_cast<py::ssize_t>(ta->get_order() + 1u);
                const auto bs = boost::numeric_cast<py::ssize_t>(ta->get_batch_size());

                auto ret = py::array_t<double>(py::array::ShapeContainer{nvars, ncoeff, bs}, ta->get_tc().data(), o);

                // Ensure the returned array is read-only.
                ret.attr("flags").attr("writeable") = false;

                return ret;
            })
        .def_property_readonly(
            "last_h",
            [](const py::object &o) {
                auto *ta = py::cast<const hey::taylor_adaptive_batch<double> *>(o);

                auto ret = py::array_t<double>(
                    py::array::ShapeContainer{boost::numeric_cast<py::ssize_t>(ta->get_batch_size())},
                    ta->get_last_h().data(), o);

                // Ensure the returned array is read-only.
                ret.attr("flags").attr("writeable") = false;

                return ret;
            })
        .def_property_readonly(
            "d_output",
            [](const py::object &o) {
                auto *ta = py::cast<const hey::taylor_adaptive_batch<double> *>(o);

                const auto nvars = boost::numeric_cast<py::ssize_t>(ta->get_dim());
                const auto bs = boost::numeric_cast<py::ssize_t>(ta->get_batch_size());

                auto ret = py::array_t<double>(py::array::ShapeContainer{nvars, bs}, ta->get_d_output().data(), o);

                // Ensure the returned array is read-only.
                ret.attr("flags").attr("writeable") = false;

                return ret;
            })
        .def(
            "update_d_output",
            [](py::object &o, const std::vector<double> &t, bool rel_time) {
                auto *ta = py::cast<hey::taylor_adaptive_batch<double> *>(o);

                ta->update_d_output(t, rel_time);

                const auto nvars = boost::numeric_cast<py::ssize_t>(ta->get_dim());
                const auto bs = boost::numeric_cast<py::ssize_t>(ta->get_batch_size());

                auto ret = py::array_t<double>(py::array::ShapeContainer{nvars, bs}, ta->get_d_output().data(), o);

                // Ensure the returned array is read-only.
                ret.attr("flags").attr("writeable") = false;

                return ret;
            },
            "t"_a, "rel_time"_a = false)
        .def_property_readonly("order", &hey::taylor_adaptive_batch<double>::get_order)
        .def_property_readonly("dim", &hey::taylor_adaptive_batch<double>::get_dim)
        .def_property_readonly("batch_size", &hey::taylor_adaptive_batch<double>::get_batch_size)
        // Repr.
        .def("__repr__",
             [](const hey::taylor_adaptive_batch<double> &ta) {
                 std::ostringstream oss;
                 oss << ta;
                 return oss.str();
             })
        // Copy/deepcopy.
        .def("__copy__", [](const hey::taylor_adaptive_batch<double> &ta) { return ta; })
        .def(
            "__deepcopy__", [](const hey::taylor_adaptive_batch<double> &ta, py::dict) { return ta; }, "memo"_a);

    // Expose the llvm state getter.
    heypy::expose_llvm_state_property(tabd_c);

    // Setup the sympy integration bits.
    heypy::setup_sympy(m);

    // Setup the networkx integration bits.
    heypy::setup_networkx(m);
}

#if defined(__clang__)

#pragma clang diagnostic pop

#endif
