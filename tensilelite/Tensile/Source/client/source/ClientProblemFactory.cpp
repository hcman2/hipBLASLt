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

#include "ClientProblemFactory.hpp"
#include "DataInitialization.hpp"

#include <cstddef>

namespace Tensile
{
    namespace Client
    {
        ClientProblemFactory::ClientProblemFactory(po::variables_map const& args)
            : m_problemSizes(args["problem-size"].as<std::vector<std::vector<size_t>>>())
            , m_stridedBatched(args["strided-batched"].as<bool>())
            , m_groupedGemm(args["grouped-gemm"].as<bool>())
            , m_aSparse(args["sparse-a"].as<bool>())
            , m_b2bGemm(args["b2b-gemm"].as<bool>())
            , m_highPrecisionAccumulate(args["high-precision-accumulate"].as<bool>())
            , m_kernelLanguage(args["kernel-language"].as<KernelLanguage>())
            , m_performanceMetric(args["performance-metric"].as<PerformanceMetric>())
            , m_deterministicMode(args["deterministic-mode"].as<bool>())
            , m_cEqualsD(args["c-equal-d"].as<bool>())
            , m_biasTypeArgs(std::vector<DataType>(1, DataType::Float))
            , m_activationType(ActivationType::None)
            , m_activationNoGuard(false)
            , m_activationEnumArg(std::vector<ActivationType>(1, ActivationType::None))
            , m_f32XdlMathOp(DataType::Float)
            , m_activationComputeType(DataType::Float)
            , m_useUserArgs(false)
        {
            std::vector<bool> isComplex;
            if(args.count("problem-identifier"))
            {
                ContractionProblemGemm::IdentifierToIndices(
                    args["problem-identifier"].as<std::string>(),
                    m_freeIndices,
                    m_batchIndices,
                    m_boundIndices,
                    isComplex);

                for(size_t i = 0; i < isComplex.size(); i++)
                {
                    if(isComplex[i])
                    {
                        std::runtime_error("Complex is not supported.");
                    }
                }
            }
            else
            {
                std::runtime_error("Currently only accepts identifier as input.");
            }

            // Default datatype
            DataType type = DataType::None;
            if(args.count("type"))
            {
                type = args["type"].as<DataType>();
            }

            // Should add problem type in ClientParamters.ini
            auto dummy     = ContractionProblemGemm::GetDummy();
            auto tensors   = dummy.tensors();
            auto constants = dummy.constants();
            m_tensorTypes.resize(tensors.size());
            m_tensorStrides.resize(tensors.size());
            m_constantTypes.resize(constants.size());
            m_constantValues.resize(constants.size());
            // Get types and values from the information from ContractionProblem
            // May contain useless information for ClientProblemFactory
            // Get tensor types
            for(size_t i = 0; i < tensors.size(); i++)
            {
                std::string typeName = tensors[i].getName() + "-type";
                if(args.count(typeName))
                {
                    m_tensorTypes[i] = args[typeName].as<DataType>();
                }
                else
                {
                    m_tensorTypes[i] = type;
                }
                std::string strideName = tensors[i].getName() + "-strides";
                if(args.count(strideName))
                {
                    m_tensorStrides[i] = args[strideName].as<std::vector<std::vector<size_t>>>();
                }
                else
                {
                    m_tensorStrides[i] = std::vector<std::vector<size_t>>();
                }
            }
            // Get constant types
            for(size_t i = 0; i < constants.size(); i++)
            {
                std::string typeName = constants[i].name + "-type";
                if(args.count(typeName))
                {
                    m_constantTypes[i] = args[typeName].as<DataType>();
                }
                else
                {
                    m_constantTypes[i] = type;
                }
                std::string valueName = "init-" + constants[i].name;
                if(args.count(valueName))
                {
                    m_constantValues[i]
                        = DataInitialization::getValue<double>(args[valueName].as<InitMode>());
                }
                else
                {
                    m_constantValues[i] = 0;
                }
            }

            if(args.count("activation-compute-type"))
                m_activationComputeType = args["activation-compute-type"].as<DataType>();

            if(args.count("use-e"))
                m_useE = args["use-e"].as<bool>();

            if(args.count("use-gradient"))
                m_useGradient = args["use-gradient"].as<bool>();

            if(args.count("bias-type-args"))
                m_biasTypeArgs = args["bias-type-args"].as<std::vector<DataType>>();

            if(args.count("activation-type"))
                m_activationType = args["activation-type"].as<ActivationType>();
            if(args.count("activation-no-guard"))
                m_activationNoGuard = args["activation-no-guard"].as<bool>();
            if(args.count("activation-enum-args"))
                m_activationEnumArg
                    = args["activation-enum-args"].as<std::vector<ActivationType>>();
            if(args.count("use-bias"))
                m_useBias = args["use-bias"].as<bool>();
            if(args.count("bias-source"))
                m_biasSrc = args["bias-source"].as<int>();
            if(args.count("use-scaleDVec"))
                m_useScaleDVec = args["use-scaleDVec"].as<bool>();
            if(args.count("max-workspace-size"))
                m_maxWorkspaceSize = args["max-workspace-size"].as<size_t>();

            if(args.count("f32-xdl-math-op"))
            {
                m_f32XdlMathOp = args["f32-xdl-math-op"].as<DataType>();
            }

            if(args.count("use-user-args"))
            {
                m_useUserArgs = args["use-user-args"].as<bool>();
            }

            if(m_groupedGemm)
            {
                auto problems = std::make_shared<ContractionProblemGroupedGemm>();
                createProblems(problems->gemms);
                m_problems.push_back(static_pointer_cast<ContractionProblem>(problems));
            }
            else if(m_b2bGemm)
            {
                auto problems = std::make_shared<ContractionProblemB2BGemm>();
                createProblems(problems->gemms);
                m_problems.push_back(static_pointer_cast<ContractionProblem>(problems));
            }
            else
            {
                std::vector<ContractionProblemGemm> v;
                createProblems(v);
                for(auto& it : v)
                {
                    auto problem     = std::make_shared<ContractionProblemGemm>();
                    (*problem.get()) = it;
                    m_problems.push_back(static_pointer_cast<ContractionProblem>(problem));
                }
            }
        }

        ClientProblemFactory::~ClientProblemFactory() = default;

        std::vector<std::shared_ptr<ContractionProblem>> const&
            ClientProblemFactory::problems() const
        {
            return m_problems;
        }

        void ClientProblemFactory::createProblems(std::vector<ContractionProblemGemm>& rv)
        {
            rv.clear();
            int biasSize       = std::max(1, (int)m_biasTypeArgs.size());
            int activationSize = std::max(1, (int)m_activationEnumArg.size());
            if(m_b2bGemm)
            {
                if(activationSize > 2)
                {
                    std::runtime_error("B2B Gemm only supports ActivationSize <= 2.");
                }
                if(m_problemSizes.size() == 1)
                {
                    std::vector<size_t> problem2 = m_problemSizes[0];
                    problem2[3] = problem2[1];
                    m_problemSizes.push_back(problem2);
                }
                rv.reserve(2);
            }
            else
            {
                rv.reserve(m_problemSizes.size() * activationSize);
            }

            std::vector<size_t> aStrides, bStrides, cStrides, dStrides, eStrides;

            if(m_tensorStrides[ContractionProblemGemm::TENSOR::A].size() == 1)
                aStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::A][0];
            if(m_tensorStrides[ContractionProblemGemm::TENSOR::B].size() == 1)
                bStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::B][0];
            if(m_tensorStrides[ContractionProblemGemm::TENSOR::C].size() == 1)
                cStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::C][0];
            if(m_tensorStrides[ContractionProblemGemm::TENSOR::D].size() == 1)
                dStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::D][0];
            if(m_tensorStrides[ContractionProblemGemm::TENSOR::E].size() == 1)
                eStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::E][0];
            if(m_b2bGemm)
            {
                for(int k = 0; k < biasSize; k++)
                {                 
                    size_t M = m_problemSizes[0][0];
                    size_t N = m_problemSizes[0][1];
                    size_t B = m_problemSizes[0][2];
                    size_t K = m_problemSizes[0][3];

                    aStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::A][0];
                    bStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::B][0];
                    cStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::C][0];
                    dStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::D][0];

                    //1st Gemm
                    rv.push_back(ContractionProblemGemm::FromIndexSizes(
                        m_freeIndices,
                        m_batchIndices,
                        m_boundIndices,
                        m_problemSizes[0],
                        m_tensorTypes[ContractionProblemGemm::TENSOR::A],
                        aStrides,
                        m_tensorTypes[ContractionProblemGemm::TENSOR::B],
                        bStrides,
                        m_tensorTypes[ContractionProblemGemm::TENSOR::C],
                        cStrides,
                        m_tensorTypes[ContractionProblemGemm::TENSOR::D],
                        dStrides,
                        m_constantValues[ContractionProblemGemm::CONST::BETA]));

                    rv.back().setAlphaRestriction(toScalarValueEnum(
                        m_constantValues[ContractionProblemGemm::CONST::ALPHA]));
                    rv.back().setAlphaType(
                        m_constantTypes[ContractionProblemGemm::CONST::ALPHA]);
                    rv.back().setBetaType(m_constantTypes[ContractionProblemGemm::CONST::BETA]);
                    rv.back().setStridedBatched(m_stridedBatched);
                    rv.back().setHighPrecisionAccumulate(m_highPrecisionAccumulate);
                    rv.back().setUseE(m_useE);
                    rv.back().setKernelLanguage(m_kernelLanguage);
                    rv.back().setPerformanceMetric(m_performanceMetric);
                    rv.back().setDeterministicMode(m_deterministicMode);
                    rv.back().setFp16AltImpl(m_fp16AltImpl);
                    rv.back().setWorkspaceSize(m_maxWorkspaceSize);
                    rv.back().setScaleDVec(m_constantTypes[ContractionProblemGemm::CONST::ALPHA],
                                        rv.back().d().sizes()[0]);
                    rv.back().setGroupedGemm(m_groupedGemm);
                    rv.back().setB2BGemm(m_b2bGemm);

                    //Fixed config for 1st GEMM
                    rv.back().setCEqualsD(false);
                    rv.back().setUseBias(false);
                    rv.back().setBias(DataType::None, 0);
                    rv.back().setActivationType(ActivationType::None);
                    rv.back().setUseScaleDVec(false);

                    //2nd Gemm
                    aStrides[1] = N;
                    if(m_tensorStrides[ContractionProblemGemm::TENSOR::B].size() == 2)
                        bStrides[1] = m_tensorStrides[ContractionProblemGemm::TENSOR::B][1][1];
                    else
                        bStrides[1] = N;
                    rv.push_back(ContractionProblemGemm::FromIndexSizes(
                        m_freeIndices,
                        m_batchIndices,
                        m_boundIndices,
                        m_problemSizes[1],
                        m_tensorTypes[ContractionProblemGemm::TENSOR::A],
                        aStrides,
                        m_tensorTypes[ContractionProblemGemm::TENSOR::B],
                        bStrides,
                        m_tensorTypes[ContractionProblemGemm::TENSOR::C],
                        cStrides,
                        m_tensorTypes[ContractionProblemGemm::TENSOR::D],
                        dStrides,
                        m_constantValues[ContractionProblemGemm::CONST::BETA]));
                    rv.back().setAlphaRestriction(toScalarValueEnum(
                        m_constantValues[ContractionProblemGemm::CONST::ALPHA]));
                    rv.back().setCEqualsD(m_cEqualsD);
                    rv.back().setAlphaType(
                        m_constantTypes[ContractionProblemGemm::CONST::ALPHA]);
                    rv.back().setBetaType(m_constantTypes[ContractionProblemGemm::CONST::BETA]);
                    rv.back().setStridedBatched(m_stridedBatched);
                    rv.back().setHighPrecisionAccumulate(m_highPrecisionAccumulate);
                    rv.back().setUseBias(m_useBias);
                    rv.back().setUseE(m_useE);
                    rv.back().setKernelLanguage(m_kernelLanguage);
                    rv.back().setPerformanceMetric(m_performanceMetric);
                    rv.back().setDeterministicMode(m_deterministicMode);
                    rv.back().setSparseA(m_aSparse);
                    rv.back().setFp16AltImpl(m_fp16AltImpl);
                    rv.back().setActivationType(m_activationType);
                    rv.back().setWorkspaceSize(m_maxWorkspaceSize);
                    if(k < m_biasTypeArgs.size())
                    {
                        rv.back().setBias(m_biasTypeArgs[k], rv.back().d().sizes()[0]);
                    }
                    else
                    {
                        rv.back().setBias(DataType::None, 0);
                    }
                    if(m_useE)
                    {
                        bool isEOutput = true;
                        rv.back().setE(m_tensorTypes[ContractionProblemGemm::TENSOR::E],
                                    rv.back().d().sizes(),
                                    eStrides,
                                    isEOutput);
                    }
                    if(0 < m_activationEnumArg.size())
                    {
                        rv.back().setActivationEnumArg(m_activationEnumArg[0]);
                    }
                    else
                    {
                        rv.back().setActivationType(m_activationType);
                    }
                    rv.back().setUseScaleDVec(m_useScaleDVec);
                    rv.back().setScaleDVec(m_constantTypes[ContractionProblemGemm::CONST::ALPHA],
                                        rv.back().d().sizes()[0]);
                    
                    rv.back().setGroupedGemm(m_groupedGemm);
                    rv.back().setF32XdlMathOp(m_f32XdlMathOp);
                    rv.back().setActivationComputeType(m_activationComputeType);
                    rv.back().setUseDeviceUserArguments(m_useUserArgs);
                    rv.back().setB2BGemm(m_b2bGemm);
                }

            }
            else
            {

                for(int k = 0; k < biasSize; k++)
                {
                    for(int j = 0; j < activationSize; j++)
                    {
                        for(int i = 0; i < m_problemSizes.size(); i++)
                        {
	                        if(m_tensorStrides[ContractionProblemGemm::TENSOR::A].size()
	                           == m_problemSizes.size())
	                            aStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::A][i];
	                        if(m_tensorStrides[ContractionProblemGemm::TENSOR::B].size()
	                           == m_problemSizes.size())
	                            bStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::B][i];
	                        if(m_tensorStrides[ContractionProblemGemm::TENSOR::C].size()
	                           == m_problemSizes.size())
	                            cStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::C][i];
	                        if(m_tensorStrides[ContractionProblemGemm::TENSOR::D].size()
	                           == m_problemSizes.size())
	                            dStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::D][i];
	                        if(m_tensorStrides[ContractionProblemGemm::TENSOR::E].size()
	                           == m_problemSizes.size())
	                            eStrides = m_tensorStrides[ContractionProblemGemm::TENSOR::E][i];
	
	                        rv.push_back(ContractionProblemGemm::FromIndexSizes(
	                            m_freeIndices,
	                            m_batchIndices,
	                            m_boundIndices,
	                            m_problemSizes[i],
	                            m_tensorTypes[ContractionProblemGemm::TENSOR::A],
	                            aStrides,
	                            m_tensorTypes[ContractionProblemGemm::TENSOR::B],
	                            bStrides,
	                            m_tensorTypes[ContractionProblemGemm::TENSOR::C],
	                            cStrides,
	                            m_tensorTypes[ContractionProblemGemm::TENSOR::D],
	                            dStrides,
	                            m_constantValues[ContractionProblemGemm::CONST::BETA]));
	
                            rv.back().setAlphaRestriction(toScalarValueEnum(
                                m_constantValues[ContractionProblemGemm::CONST::ALPHA]));
                            rv.back().setCEqualsD(m_cEqualsD);
                            rv.back().setAlphaType(
                                m_constantTypes[ContractionProblemGemm::CONST::ALPHA]);
                            rv.back().setBetaType(m_constantTypes[ContractionProblemGemm::CONST::BETA]);
                            rv.back().setStridedBatched(m_stridedBatched);
                            rv.back().setHighPrecisionAccumulate(m_highPrecisionAccumulate);
                            rv.back().setUseGradient(m_useGradient);
                            rv.back().setUseBias(m_useBias);
                            rv.back().setUseE(m_useE);
                            rv.back().setKernelLanguage(m_kernelLanguage);
                            rv.back().setPerformanceMetric(m_performanceMetric);
                            rv.back().setDeterministicMode(m_deterministicMode);
                            rv.back().setSparseA(m_aSparse);
                            rv.back().setFp16AltImpl(m_fp16AltImpl);
                            rv.back().setActivationType(m_activationType);
                            rv.back().setWorkspaceSize(m_maxWorkspaceSize);
                            if(k < m_biasTypeArgs.size())
                            {
                                auto length       = (m_biasSrc == ContractionProblemGemm::TENSOR::B)
                                                        ? rv.back().d().sizes()[1]
                                                        : rv.back().d().sizes()[0];
                                bool isBiasOutput = m_useGradient ? true : false;
                                rv.back().setBias(
                                    m_biasTypeArgs[k],
                                    length,
                                    isBiasOutput,
                                    static_cast<ContractionProblemGemm::TENSOR>(m_biasSrc));
                            }
                            else
                            {
                                rv.back().setBias(DataType::None, 0);
                            }
                            if(m_useE)
                            {
                                bool isEOutput = true;
                                if(m_useGradient)
                                    isEOutput = false;
                                rv.back().setE(m_tensorTypes[ContractionProblemGemm::TENSOR::E],
                                            rv.back().d().sizes(),
                                            eStrides,
                                            isEOutput);
                            }
                            if(j < m_activationEnumArg.size())
                            {
                                rv.back().setActivationEnumArg(m_activationEnumArg[j]);
                            }
                            else
                            {
                                rv.back().setActivationType(m_activationType);
                            }
                            rv.back().setActivationNoGuard(m_activationNoGuard);
                            rv.back().setUseScaleDVec(m_useScaleDVec);
                            rv.back().setScaleDVec(
                                m_constantTypes[ContractionProblemGemm::CONST::ALPHA],
                                rv.back().d().sizes()[0]);

                            rv.back().setGroupedGemm(m_groupedGemm);
                            rv.back().setF32XdlMathOp(m_f32XdlMathOp);
                            rv.back().setActivationComputeType(m_activationComputeType);
                            rv.back().setUseDeviceUserArguments(m_useUserArgs);
	                        rv.back().setB2BGemm(m_b2bGemm);
	                    }
                    }
                }
            }
        }
    } // namespace Client
} // namespace Tensile
