/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/

#include <Tensile/Contractions.hpp>
#include <Tensile/EmbeddedLibrary.hpp>
#include <Tensile/MasterSolutionLibrary.hpp>
#include <Tensile/Tensile.hpp>
#include <Tensile/hip/HipHardware.hpp>
#include <Tensile/hip/HipSolutionAdapter.hpp>
#include <Tensile/hip/HipUtils.hpp>

#include "BenchmarkTimer.hpp"
#include "ClientProblemFactory.hpp"
#include "DataInitialization.hpp"
#include "HardwareMonitorListener.hpp"
#include "MetaRunListener.hpp"
#include "ProgressListener.hpp"
#include "ReferenceValidator.hpp"
#include "SolutionIterator.hpp"
#include "TimingEvents.hpp"

#include "LibraryUpdateReporter.hpp"
#include "LogReporter.hpp"
#include "MetaResultReporter.hpp"
#include "PerformanceReporter.hpp"
#include "ResultFileReporter.hpp"
#include "ResultReporter.hpp"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/program_options.hpp>

#include <cstddef>

namespace po = boost::program_options;

namespace Tensile
{
    namespace Client
    {

        template <typename T>
        po::typed_value<T>* value_default(std::string const& desc)
        {
            return po::value<T>()->default_value(T(), desc);
        }

        template <typename T>
        po::typed_value<T>* value_default()
        {
            return po::value<T>()->default_value(T());
        }

        template <typename T>
        po::typed_value<std::vector<T>>* vector_default_empty()
        {
            return value_default<std::vector<T>>("[]");
        }

        po::options_description all_options()
        {
            po::options_description options("Tensile client options");

            // clang-format off
            options.add_options()
                ("help,h", "Show help message.")

                ("config-file",              vector_default_empty<std::string>(), "INI config file(s) to read.")

                ("library-file,l",           po::value<std::string>(), "Load a (YAML) solution library.  If not specified, we will use "
                                                                       "the embedded library, if available.")
                ("code-object,c",            vector_default_empty<std::string>(), "Code object file with kernel(s).  If none are "
                                                                                  "specified, we will use the embedded code "
                                                                                  "object(s) if available.")

                ("performance-metric",       po::value<PerformanceMetric>()->default_value(PerformanceMetric::DeviceEfficiency), "Metric for benchmarking results")

                ("problem-identifier",       po::value<std::string>(), "Problem identifer (Einstein notation). Either "
                                                                       "this or free/batch/bound must be specified.")

                ("type",                     po::value<DataType>()->default_value(DataType::None), "Data type")
                ("a-type",                   po::value<DataType>()->default_value(DataType::None), "A data type")
                ("b-type",                   po::value<DataType>()->default_value(DataType::None), "B data type")
                ("c-type",                   po::value<DataType>()->default_value(DataType::None), "C data type")
                ("d-type",                   po::value<DataType>()->default_value(DataType::None), "D data type")
                ("e-type",                   po::value<DataType>()->default_value(DataType::None), "E data type")
                ("alpha-type",               po::value<DataType>()->default_value(DataType::None), "alpha data type")
                ("beta-type",                po::value<DataType>()->default_value(DataType::None), "beta data type")
                ("high-precision-accumulate", po::value<bool>()->default_value(false), "Use high-precision accumulate.")
                ("strided-batched",          po::value<bool>()->default_value(true), "Use strided-batched or general batched")
                ("grouped-gemm",             po::value<bool>()->default_value(false), "Use grouped gemm")
                ("kernel-language",          po::value<KernelLanguage>()->default_value(KernelLanguage::Any), "Select kernel language.")
                ("deterministic-mode",       po::value<bool>()->default_value(false), "Enforce deterministic summation patterns"
                                                                                      "by not splitting U among workgroups")

                ("init-a",                   po::value<InitMode>()->default_value(InitMode::Random), "Initialization for A")
                ("init-b",                   po::value<InitMode>()->default_value(InitMode::Random), "Initialization for B")
                ("init-c",                   po::value<InitMode>()->default_value(InitMode::Random), "Initialization for C")
                ("init-d",                   po::value<InitMode>()->default_value(InitMode::Zero), "Initialization for D")
                ("init-e",                   po::value<InitMode>()->default_value(InitMode::Zero), "Initialization for E")
                ("init-alpha",               po::value<InitMode>()->default_value(InitMode::Two), "Initialization for alpha")
                ("init-beta",                po::value<InitMode>()->default_value(InitMode::Two), "Initialization for beta")
                ("init-bias",                po::value<InitMode>()->default_value(InitMode::One), "Initialization for bias")
                ("init-scaleD",              po::value<InitMode>()->default_value(InitMode::One), "Initialization for scaleD")
                ("pristine-on-gpu",          po::value<bool>()->default_value(true), "Keep a pristine copy of inputs on GPU for performance")
                ("c-equal-d",                po::value<bool>()->default_value(false), "C equals D")
                ("offset-a",                 po::value<size_t>()->default_value(0), "buffer a start offset")
                ("offset-b",                 po::value<size_t>()->default_value(0), "buffer b start offset")
                ("offset-c",                 po::value<size_t>()->default_value(0), "buffer c start offset")
                ("offset-d",                 po::value<size_t>()->default_value(0), "buffer d start offset")
                ("offset-e",                 po::value<size_t>()->default_value(0), "buffer e start offset")
                ("print-valids",             po::value<bool>()->default_value(false), "Print values that pass validation")
                ("print-max",                po::value<int>()->default_value(-1), "Max number of values to print")
                ("num-elements-to-validate", po::value<int>()->default_value(0), "Number of elements to validate")
                ("bounds-check",             po::value<BoundsCheckMode>()->default_value(BoundsCheckMode::Disable),
                "1:Use sentinel values to check memory boundaries."
                "2:Memory bound check by front guard page"
                "3:Memory bound check by back guard page"
                "4:Memory bound check by both side guard page")

                ("print-tensor-a",           po::value<bool>()->default_value(false), "Print tensor A.")
                ("print-tensor-b",           po::value<bool>()->default_value(false), "Print tensor B.")
                ("print-tensor-c",           po::value<bool>()->default_value(false), "Print tensor C.")
                ("print-tensor-d",           po::value<bool>()->default_value(false), "Print tensor D.")
                ("print-tensor-ref",         po::value<bool>()->default_value(false), "Print reference tensor D.")

                ("dump-tensors",             po::value<bool>()->default_value(false), "Binary dump tensors instead of printing.")

                ("device-idx",               po::value<int>()->default_value(0), "Device index")
                ("use-default-stream",       po::value<bool>()->default_value(false), "Use default Hip stream to run kernels.")
                ("platform-idx",             po::value<int>()->default_value(0), "OpenCL Platform Index")

                ("num-warmups",              po::value<int>()->default_value(0), "Number of warmups to run")
                ("sync-after-warmups",       po::value<bool>()->default_value(true), "Synchronize GPU after warmup kernel runs")
                ("num-benchmarks",           po::value<int>()->default_value(1), "Number of benchmarks to run")
                ("num-enqueues-per-sync",    po::value<int>()->default_value(1), "Enqueues per sync, will affect by min-flops-per-sync")
                ("max-enqueues-per-sync",    po::value<int>()->default_value(-1), "Max Enqueues per sync, will affect by min-flops-per-sync")
                ("num-syncs-per-benchmark",  po::value<int>()->default_value(1), "Syncs per benchmark")
                ("min-flops-per-sync",       po::value<size_t>()->default_value(0), "Minimum number of flops per sync to increase stability for small problems.")
                ("use-gpu-timer",            po::value<bool>()->default_value(true), "Use GPU timer")
                ("sleep-percent",            po::value<int>()->default_value(0), "Sleep percentage")
                ("hardware-monitor",         po::value<bool>()->default_value(true), "Use hardware monitor.")

                ("perf-l2-read-hits",        po::value<double>()->default_value(0.0), "L2 read hits")
                ("perf-l2-write-hits",       po::value<double>()->default_value(0.5), "L2 write hits")
                ("perf-l2-read-bw-mul",      po::value<double>()->default_value(2.0), "L2 read bandwidth multiplier")
                ("perf-read-efficiency",     po::value<double>()->default_value(0.85), "Read efficiency")
                ("perf-ops-per-cycle",       po::value<int>()->default_value(64), "Ops per cycle")
                ("csv-export-extra-cols",    po::value<bool>()->default_value(false), "CSV exports winner information")
                ("csv-merge-same-problems",  po::value<bool>()->default_value(false), "CSV merge rows of same problem id")
                ("PrintWinnersOnly",         po::value<bool>()->default_value(false), "PrintWinnersOnly")

                ("problem-size,p",           vector_default_empty<std::string>(), "Specify a problem size.  Comma-separated list of "
                                                                                  "sizes, in the order of the Einstein notation.")

                ("a-strides",                vector_default_empty<std::string>(), "Unspecified means default stride "
                                                                                  "(prev_dim_stride*prev_dim_size)"
                                                                                  "specifying once applies to all problem sizes, "
                                                                                  "otherwise specify once per problem size.")

                ("b-strides",                vector_default_empty<std::string>(), "Unspecified means default stride "
                                                                                  "(prev_dim_stride*prev_dim_size)"
                                                                                  "specifying once applies to all problem sizes, "
                                                                                  "otherwise specify once per problem size.")

                ("c-strides",                vector_default_empty<std::string>(), "Unspecified means default stride "
                                                                                  "(prev_dim_stride*prev_dim_size)"
                                                                                  "specifying once applies to all problem sizes, "
                                                                                  "otherwise specify once per problem size.")

                ("d-strides",                vector_default_empty<std::string>(), "Unspecified means default stride "
                                                                                  "(prev_dim_stride*prev_dim_size)"
                                                                                  "specifying once applies to all problem sizes, "
                                                                                  "otherwise specify once per problem size.")

                ("e-strides",                vector_default_empty<std::string>(), "Unspecified means default stride "
                                                                                  "(prev_dim_stride*prev_dim_size)"
                                                                                  "specifying once applies to all problem sizes, "
                                                                                  "otherwise specify once per problem size.")

                ("problem-start-idx",        po::value<int>()->default_value(0),  "First problem to run")
                ("num-problems",             po::value<int>()->default_value(-1), "Number of problems to run")

                ("solution-start-idx",       po::value<int>()->default_value(-1), "First solution to run")
                ("num-solutions",            po::value<int>()->default_value(-1), "Number of solutions to run")
                ("best-solution",            po::value<bool>()->default_value(false), "Best solution benchmark mode")

                ("results-file",             po::value<std::string>()->default_value("results.csv"), "File name to write results.")
                ("log-file",                 po::value<std::string>(),                               "File name for output log.")
                ("log-file-append",          po::value<bool>()->default_value(false),                "Append to log file.")
                ("log-level",                po::value<LogLevel>()->default_value(LogLevel::Debug),  "Log level")

                ("library-update-file",      po::value<std::string>()->default_value(""), "File name for writing indices "
                                                                                          "and speeds suitable for updating "
                                                                                          "an existing library logic file.")
                ("library-update-comment",   po::value<bool>()->default_value(false), "Include solution name as a "
                                                                                      "comment in library update "
                                                                                      "file.")


                ("exit-on-error",            po::value<bool>()->default_value(false), "Exit run early on failed kernels or other errors.")
                ("selection-only",           po::value<bool>()->default_value(false), "Don't run any solutions, only print kernel selections.")
                ("max-workspace-size",       po::value<size_t>()->default_value(32*1024*1024), "Max workspace for training")
                ("granularity-threshold",    po::value<double>()->default_value(0.0), "Don't run a solution if total granularity is below")

                ("activation-type",           po::value<ActivationType>()->default_value(ActivationType::None), "An activation type")
                ("activation-hpa",            po::value<bool>()->default_value(false), "Use the same data type as high precision accumulate.")
                ("activation-guard",          po::value<bool>()->default_value(false), "Use activation guard to deall with nan outputs.")
                ("activation-additional-args",vector_default_empty<std::string>(), "Activation additional floating-point number arguments.")
                ("activation-enum-args",      po::value<std::vector<ActivationType>>()->default_value(std::vector<ActivationType>(1, ActivationType::None), "[]"), "Activation enum argument.")
                ("use-bias",                  po::value<bool>()->default_value(false), "Use bias.")
                ("bias-source",               po::value<int>()->default_value(3), "Bias source.")
                ("use-scaleD",                po::value<bool>()->default_value(false), "Use scaleD.")
                ("bias-type-args",            po::value<std::vector<DataType>>()->default_value(std::vector<DataType>(1, DataType::None), "[]"), "Bias data type args.")
                ("use-e",                     po::value<bool>()->default_value(false), "Use E.")
                ("use-gradient",              po::value<bool>()->default_value(false), "Use gradient.")
                ;
            // clang-format on

            return options;
        }

        std::shared_ptr<Hardware> GetHardware(po::variables_map const& args)
        {
            int deviceCount = 0;
            HIP_CHECK_EXC(hipGetDeviceCount(&deviceCount));

            int deviceIdx = args["device-idx"].as<int>();

            if(deviceIdx >= deviceCount)
                throw std::runtime_error(concatenate(
                    "Invalid device index ", deviceIdx, " (", deviceCount, " total found.)"));

            HIP_CHECK_EXC(hipSetDevice(deviceIdx));

            return hip::GetCurrentDevice();
        }

        hipStream_t GetStream(po::variables_map const& args)
        {
            if(args["use-default-stream"].as<bool>())
                return 0;

            hipStream_t stream;
            HIP_CHECK_EXC(hipStreamCreate(&stream));
            return stream;
        }

        std::shared_ptr<MasterSolutionLibrary<ContractionProblemGemm>>
            LoadSolutionLibrary(po::variables_map const& args)
        {
            auto filename = args["library-file"];
            if(!filename.empty())
            {
                return std::dynamic_pointer_cast<MasterSolutionLibrary<ContractionProblemGemm>>(
                    LoadLibraryFile<ContractionProblemGemm>(filename.as<std::string>()));
            }

            auto embeddedLibrary
                = std::dynamic_pointer_cast<MasterSolutionLibrary<ContractionProblemGemm>>(
                    EmbeddedLibrary<ContractionProblemGemm>::Get());

            if(embeddedLibrary != nullptr)
                return embeddedLibrary;

            throw std::runtime_error("Client must be linked with an embedded library or "
                                     "a library must be specified at runtime.");
        }

        void LoadCodeObjects(po::variables_map const& args, hip::SolutionAdapter& adapter)
        {
            auto const& filenames = args["code-object"].as<std::vector<std::string>>();
            auto        logLevel  = args["log-level"].as<LogLevel>();

            if(filenames.empty())
            {
                adapter.loadEmbeddedCodeObjects();
            }
            else
            {
                //only trigger exception when failed to load all code objects.
                bool       loaded   = false;
                hipError_t retError = hipSuccess;

                for(auto const& filename : filenames)
                {
                    hipError_t ret;

                    if(logLevel >= LogLevel::Verbose)
                        std::cout << "Loading " << filename << std::endl;
                    ret = adapter.loadCodeObjectFile(filename);

                    if(ret == hipSuccess)
                        loaded = true;
                    else
                        retError = ret;
                }

                if(!loaded)
                    HIP_CHECK_EXC(retError);
            }
        }

        template <typename T>
        std::vector<T> split_nums(std::string const& value)
        {
            std::vector<std::string> parts;
            boost::split(parts, value, boost::algorithm::is_any_of(",;"));

            std::vector<T> rv;
            rv.reserve(parts.size());

            for(auto const& part : parts)
                if(part != "")
                    rv.push_back(boost::lexical_cast<T>(part));

            return rv;
        }

        template <typename T>
        void parse_arg_nums(po::variables_map& args, std::string const& name)
        {
            auto inValue = args[name].as<std::vector<std::string>>();

            std::vector<std::vector<T>> outValue;
            outValue.reserve(inValue.size());
            for(auto const& str : inValue)
                outValue.push_back(split_nums<T>(str));

            boost::any v(outValue);

            args.at(name).value() = v;
        }

        void parse_arg_ints(po::variables_map& args, std::string const& name)
        {
            parse_arg_nums<size_t>(args, name);
        }

        void parse_arg_double(po::variables_map& args, std::string const& name)
        {
            parse_arg_nums<double>(args, name);
        }

        void parse_bias_type_args(po::variables_map& args, std::string const& name)
        {
            auto type             = args[name].as<std::vector<DataType>>();
            args.at(name).value() = boost::any(type);
        }

        void parse_activation_enum_args(po::variables_map& args, std::string const& name)
        {
            auto type             = args[name].as<std::vector<ActivationType>>();
            args.at(name).value() = boost::any(type);
        }

        void parse_activation_int(po::variables_map& args, std::string const& name)
        {
            auto type = args[name].as<ActivationType>();

            args.at(name).value() = boost::any(type);
        }

        void fix_data_types(po::variables_map& args)
        {
            auto type = args["type"].as<DataType>();

            // These types use the same data type for all inputs/outputs, so we allow
            // using the overarching 'type' parameter.
            if(type == DataType::Float || type == DataType::Double || type == DataType::ComplexFloat
               || type == DataType::ComplexDouble || type == DataType::Int32)
            {
                args.at("a-type").value()     = boost::any(type);
                args.at("b-type").value()     = boost::any(type);
                args.at("c-type").value()     = boost::any(type);
                args.at("d-type").value()     = boost::any(type);
                args.at("alpha-type").value() = boost::any(type);
                args.at("beta-type").value()  = boost::any(type);
            }
        }

        po::variables_map parse_args(int argc, const char* argv[])
        {
            auto options = all_options();

            po::variables_map args;
            po::store(po::parse_command_line(argc, argv, options), args);
            po::notify(args);

            if(args.count("help"))
            {
                std::cout << options << std::endl;
                exit(1);
            }

            if(args.count("config-file"))
            {
                auto configFiles = args["config-file"].as<std::vector<std::string>>();
                for(auto filename : configFiles)
                {
                    std::cout << "loading config file " << filename << std::endl;
                    std::ifstream file(filename.c_str());
                    if(file.bad())
                        throw std::runtime_error(concatenate("Could not open ", filename));
                    po::store(po::parse_config_file(file, options), args);
                }
            }

            fix_data_types(args);

            parse_arg_ints(args, "problem-size");
            parse_arg_ints(args, "a-strides");
            parse_arg_ints(args, "b-strides");
            parse_arg_ints(args, "c-strides");
            parse_arg_ints(args, "d-strides");
            parse_arg_ints(args, "e-strides");
            parse_bias_type_args(args, "bias-type-args");
            parse_activation_int(args, "activation-type");
            parse_activation_enum_args(args, "activation-enum-args");
            parse_arg_double(args, "activation-additional-args");
            return args;
        }

    } // namespace Client
} // namespace Tensile

int main(int argc, const char* argv[])
{
    using namespace Tensile;
    using namespace Tensile::Client;

    auto args = parse_args(argc, argv);

    ClientProblemFactory problemFactory(args);

    auto        hardware = GetHardware(args);
    hipStream_t stream   = GetStream(args);

    auto                          library = LoadSolutionLibrary(args);
    Tensile::hip::SolutionAdapter adapter;
    LoadCodeObjects(args, adapter);

    auto filename = args["library-file"].as<std::string>();

    size_t      directoryPos     = filename.rfind('/');
    std::string libraryDirectory = filename;
    if(directoryPos != std::string::npos)
        libraryDirectory.resize(directoryPos + 1);
    else
        libraryDirectory = '.';

    auto result = adapter.initializeLazyLoading(hardware->archName(), libraryDirectory);
    if(result != hipSuccess)
    {
        std::string str = "Lazy loading failed. (" + std::to_string(int(result)) + ").";
        std::runtime_error(str.c_str());
    }

    auto problems        = problemFactory.problems();
    int  firstProblemIdx = args["problem-start-idx"].as<int>();
    int  numProblems     = args["num-problems"].as<int>();
    if(numProblems < 0)
        numProblems = problems.size();
    int lastProblemIdx = firstProblemIdx + numProblems - 1;

    int  firstSolutionIdx = args["solution-start-idx"].as<int>();
    int  numSolutions     = args["num-solutions"].as<int>();
    bool gpuTimer         = args["use-gpu-timer"].as<bool>();
    bool runKernels       = !args["selection-only"].as<bool>();
    bool exitOnError      = args["exit-on-error"].as<bool>();
    bool groupedGemm      = args["grouped-gemm"].as<bool>();

    if(firstSolutionIdx < 0)
        firstSolutionIdx = library->solutions.begin()->first;

    if(numSolutions < 0)
    {
        auto iter = library->solutions.end();
        iter--;
    }

    auto* ptr      = new DataInitialization(args, problemFactory);
    auto  dataInit = std::shared_ptr<DataInitialization>(ptr);

    auto solutionIterator = SolutionIterator::Default(library, hardware, args);

    MetaRunListener listeners;

    listeners.addListener(dataInit);
    listeners.addListener(solutionIterator);
    listeners.addListener(std::make_shared<ProgressListener>(args));
    if(runKernels)
    {
        listeners.addListener(std::make_shared<ReferenceValidator>(args, dataInit));
        listeners.addListener(std::make_shared<BenchmarkTimer>(args, *hardware));
        listeners.addListener(std::make_shared<HardwareMonitorListener>(args));
    }

    auto reporters = std::make_shared<MetaResultReporter>();
    reporters->addReporter(PerformanceReporter::Default(args));

    // PerformanceReporter needs to be called before these two, or else values
    // will be missing
    reporters->addReporter(LogReporter::Default(args));
    reporters->addReporter(ResultFileReporter::Default(args));
    reporters->addReporter(LibraryUpdateReporter::Default(args));

    if(args.count("log-file"))
    {
        std::string filename = args["log-file"].as<std::string>();
        auto        logFile  = std::make_shared<std::ofstream>(
            filename.c_str(), args["log-file-append"].as<bool>() ? std::ios::app : std::ios::out);

        reporters->addReporter(LogReporter::Default(args, logFile, LogLevel::Normal));
    }

    listeners.setReporter(reporters);

    // ReferenceValidator validator(args, dataInit);
    // BenchmarkTimer timer(args);

    reporters->report(ResultKey::ProblemCount, problemFactory.problems().size());

    while(listeners.needMoreBenchmarkRuns())
    {
        listeners.preBenchmarkRun();

        for(int problemIdx = firstProblemIdx; problemIdx <= lastProblemIdx; problemIdx++)
        {
            auto problem = problems[problemIdx].get();

            reporters->report(ResultKey::ProblemIndex, problemIdx);
            reporters->report(ResultKey::ProblemProgress,
                              concatenate(problemIdx, "/", lastProblemIdx));

            listeners.preProblem(problem);

            while(solutionIterator->moreSolutionsInProblem())
            {
                auto solution = solutionIterator->getSolution();
                if(solution == nullptr)
                    throw std::runtime_error("Could not find a solution");

                listeners.preSolution(*solution);
                if(solutionIterator->runCurrentSolution() && runKernels)
                {
                    try
                    {
                        while(listeners.needMoreRunsInSolution())
                        {
                            auto inputs = dataInit->prepareGPUInputs(problem);

                            auto kernels = solution->solve((*problem), *inputs, *hardware);

                            size_t       warmupInvocations = listeners.numWarmupRuns();
                            size_t       eventCount        = gpuTimer ? kernels.size() : 0;
                            TimingEvents warmupStartEvents(warmupInvocations, eventCount);
                            TimingEvents warmupStopEvents(warmupInvocations, eventCount);

                            for(int i = 0; i < warmupInvocations; i++)
                            {
                                listeners.preWarmup();
                                if(gpuTimer)
                                    HIP_CHECK_EXC(adapter.launchKernels(kernels,
                                                                        stream,
                                                                        warmupStartEvents[i],
                                                                        warmupStopEvents[i]));
                                else
                                    HIP_CHECK_EXC(
                                        adapter.launchKernels(kernels, stream, nullptr, nullptr));
                                listeners.postWarmup();
                                // Do validation after first warmup
                                if(i == 0)
                                    listeners.validateWarmups(
                                        inputs, warmupStartEvents, warmupStopEvents);
                            }

                            size_t syncs = listeners.numSyncs();
                            size_t enq   = listeners.numEnqueuesPerSync();

                            listeners.preSyncs();

                            for(int i = 0; i < syncs; i++)
                            {
                                TimingEvents startEvents(enq, eventCount);
                                TimingEvents stopEvents(enq, eventCount);

                                listeners.preEnqueues(stream);

                                for(int j = 0; j < enq; j++)
                                {
                                    HIP_CHECK_EXC(
                                        adapter.launchKernels(kernels, stream, nullptr, nullptr));
                                }

                                listeners.postEnqueues(startEvents, stopEvents, stream);
                                listeners.validateEnqueues(inputs, startEvents, stopEvents);
                            }

                            listeners.postSyncs();
                        }
                    }
                    catch(std::runtime_error const& err)
                    {
                        reporters->report(ResultKey::Validation, "INVALID");
                        reporters->log(LogLevel::Error,
                                       concatenate("Exception occurred: ", err.what(), "\n"));
                    }
                }

                listeners.postSolution();

                if(exitOnError && listeners.error() > 0)
                {
                    // error range in shell is [0-255]
                    return std::min(listeners.error(), 255);
                }
            }

            listeners.postProblem();
        }

        listeners.postBenchmarkRun();
    }

    listeners.finalizeReport();

    // error range in shell is [0-255]
    return std::min(listeners.error(), 255);
}
