import yaml
import io, copy
import subprocess

def find(d, tag):
    if tag in d:
        yield d[tag]
    for k, v in d.items():
        if isinstance(v, dict):
            for i in find(v, tag):
                yield i


currentSet = \
{
    "7168_64_1_8192"	:		"Cijk_Alik_Bljk_BBS_BH_UserArgs_MT32x64x256_MI16x16x1_SN_LDSB1_AFC0_AFEM8_AFEM8_ASEM32_GRVWA8_GRVWB8_GSU1_GSUAMB_K1_LBSPPA1024_LBSPPB512_LPA16_LPB16_LPMn1_LRVW8_LWPMn1_MIWT2_1_NTB0_NTD0_NEPBS0_SS0_SU32_SUM1_SUS512_SPO0_SVW8_VWA2_WSGRA0_WSGRB0_WG16_16_1_WGM0",
    "7168_16_1_8192":			"Cijk_Alik_Bljk_BBS_BH_UserArgs_MT32x16x256_MI16x16x1_SN_LDSB0_AFC0_AFEM1_AFEM1_ASEM1_GRVWA8_GRVWB8_GSU1_GSUAMB_K1_LBSPPA512_LBSPPB512_LPA16_LPB16_LPM0_LRVW8_LWPMn1_MIWT1_1_NTA3_NTB0_NTD3_NEPBS0_SS1_SU32_SUM1_SUS512_SPO0_SVW1_VWA1_WSGRA2_WSGRB1_WG32_4_1_WGM8",
}

d = {}
with open("input.txt") as f:
    for line in f:
        if len(line) > 10:
            (key, val) = line.split()
            d[key] = val

currentSet = d

strToParas = \
{
    #"_MT" : 'MatrixInstruction',
    "_SS" : 'SourceSwap',
    "_LDSB" : '1LDSBuffer',
    "_VWA" : 'VectorWidthA',
    "_VWB" : 'VectorWidthB',
    "_SU"  : 'StaggerU',
    "_SUS"  : 'StaggerUStride',
    "_WGM"  : 'WorkGroupMapping',
    "_WSGRA":'WaveSeparateGlobalReadA',
    "_WSGRB":'WaveSeparateGlobalReadB',
    "_GSU":'GlobalSplitU',
    "_AFEM":'AssertFree0ElementMultiple',
    "_ASEM":'AssertSummationElementMultiple',
}

def findPara(kernel,key):
    found = kernel.find(key)
    if found == -1:
        return None
    foundEnd = kernel.find('_',found+1)
    if foundEnd == -1:
        #end of string
        value = kernel[found+len(key):]
    else:
        value = kernel[found+len(key):foundEnd]
    return value

def findMT(kernel):
    found = kernel.find('_MT')
    foundEnd = kernel.find('_',found+1)
    MT = kernel[found+3:foundEnd]
    MT0 = MT.split('x')
    return MT0

def findMIWT(kernel):
    found = kernel.find('_MIWT')
    foundEnd = kernel.find('_',found+1)
    foundEnd2 = kernel.find('_',foundEnd+1)
    MT = kernel[found+5:foundEnd2]
    miwt = MT.split('_')
    return miwt

for sz,kernel in currentSet.items():
    MT = findMT(kernel)
    MIWT = findMIWT(kernel)
    mi5 = int(MIWT[0])
    mi6 = int(MIWT[1])
    mi7 = int(MT[0]) // mi5 // 16
    mi8 = int(MT[1]) // mi6 // 16
    baseMi = [[{'MatrixInstruction': [16, 16, 16, 1, 1, mi5, mi6, mi7, mi8], 'WorkGroup': [16, 16, 1], 'VectorWidthA': -1, 'VectorWidthB': -1, 'DepthU':int(MT[2])}]]
    fullSize = sz.split('_')
    isLSUenable = False
    isSecodeAFEM = False
    print("========="+sz+"==========")
    for k,v in strToParas.items():
        val = findPara(kernel,k)
        if val != None:
            if v == "AssertFree0ElementMultiple" and isSecodeAFEM == True:
                baseMi[0][0]["AssertFree1ElementMultiple"] = int(val)
            else:
                baseMi[0][0][v] = int(val)
            if v == "AssertFree0ElementMultiple":
                isSecodeAFEM = True
            
    #fix error
    if baseMi[0][0]['StaggerUStride'] == 0:
        baseMi[0][0]['StaggerUStride'] = 256
    if (mi7%4) == 0:
        #Generate a LSU4 MatrixInstruction
        isLSUenable = True
        newMi7 = mi7 // 4
        newMi5 = mi5 * 4
        newMi = copy.deepcopy(baseMi[0][0])
        newMi['WorkGroup'] = [16, 4, 4] #LSU4
        newMi['MatrixInstruction'][5] = newMi5 #miwt0
        newMi['MatrixInstruction'][7] = newMi7 #miwt0
        baseMi[0].append(newMi)
    if (mi7%2) == 0:
        #Generate a LSU2 MatrixInstruction
        isLSUenable = True
        newMi7 = mi7 // 2
        newMi5 = mi5 * 2
        newMi = copy.deepcopy(baseMi[0][0])
        newMi['WorkGroup'] = [16, 8, 2] #LSU2
        newMi['MatrixInstruction'][5] = newMi5 #miwt0
        newMi['MatrixInstruction'][7] = newMi7 #miwt0
        baseMi[0].append(newMi)
    if (mi8%4) == 0:
        #Generate a LSU4 MatrixInstruction
        isLSUenable = True
        newMi8 = mi8 // 4
        newMi6 = mi6 * 4
        newMi = copy.deepcopy(baseMi[0][0])
        newMi['WorkGroup'] = [16, 4, 4] #LSU4
        newMi['MatrixInstruction'][6] = newMi6 #miwt0
        newMi['MatrixInstruction'][8] = newMi8 #miwt0
        baseMi[0].append(newMi)
    if (mi8%2) == 0:
        #Generate a LSU2 MatrixInstruction
        isLSUenable = True
        newMi8 = mi8 // 2
        newMi6 = mi6 * 2
        newMi = copy.deepcopy(baseMi[0][0])
        newMi['WorkGroup'] = [16, 8, 2] #LSU2
        newMi['MatrixInstruction'][6] = newMi6 #miwt0
        newMi['MatrixInstruction'][8] = newMi8 #miwt0
        baseMi[0].append(newMi)
    if (mi8%2) == 0 and (mi7%2) == 0:
        #Generate a LSU4 MatrixInstruction
        isLSUenable = True
        newMi8 = mi8 // 2
        newMi6 = mi6 * 2
        newMi = copy.deepcopy(baseMi[0][0])
        newMi['WorkGroup'] = [16, 4, 4] #LSU4
        newMi['MatrixInstruction'][6] = newMi6 #miwt0
        newMi['MatrixInstruction'][8] = newMi8 #miwt0
        newMi7 = mi7 // 2
        newMi5 = mi5 * 2
        newMi['MatrixInstruction'][5] = newMi5 #miwt0
        newMi['MatrixInstruction'][7] = newMi7 #miwt0
        baseMi[0].append(newMi)
    if mi8*mi7 == 1:
        #extend to LSU4
        isLSUenable = True
        newMi = copy.deepcopy(baseMi[0][0])
        newMi['WorkGroup'] = [16, 4, 4] #LSU4
        baseMi[0].append(newMi)
    if mi8*mi7 == 1:
        #extend to LSU2
        isLSUenable = True
        newMi = copy.deepcopy(baseMi[0][0])
        newMi['WorkGroup'] = [16, 8, 2] #LSU2
        baseMi[0].append(newMi)
    print(baseMi)
    print(MT)
    print("======END=====")

    with open("tune.yaml") as stream:
    #with open("../HHS_TN_LLAMA_N1_01_Msmall_v3.yaml") as stream:
        data_loaded = yaml.safe_load(stream)
        newPara = \
        {'Groups': baseMi}
        # {'Groups': 
        # [[{'MatrixInstruction': [16, 16, 16, 1, 1, 1, 1, 1, 1], 'WorkGroup': [16, 4, 4], 'VectorWidthA': -1}, \
        # {'MatrixInstruction': [16, 16, 16, 1, 1, 2, 1, 1, 1], 'WorkGroup': [16, 4, 4], 'VectorWidthA': 2}, \
        # {'MatrixInstruction': [16, 16, 16, 1, 1, 3, 1, 1, 1], 'WorkGroup': [16, 4, 4], 'VectorWidthA': 1}, \
        # {'MatrixInstruction': [16, 16, 16, 1, 1, 4, 1, 1, 1], 'WorkGroup': [16, 4, 4]}, \
        # {'MatrixInstruction': [16, 16, 16, 1, 1, 1, 1, 2, 1], 'WorkGroup': [16, 8, 2], 'VectorWidthA': 1}, \
        # {'MatrixInstruction': [16, 16, 16, 1, 1, 2, 1, 2, 1], 'WorkGroup': [16, 8, 2], 'VectorWidthA': 2}, \
        # {'MatrixInstruction': [16, 16, 16, 1, 1, 1, 1, 4, 1], 'WorkGroup': [16, 16, 1], 'VectorWidthA': 1}]]} \

        data_loaded["BenchmarkProblems"][0][1]['ForkParameters'].append(newPara)

        try:
            print(data_loaded["BenchmarkProblems"][0][1]['BenchmarkFinalParameters'][0]['ProblemSizes'][0])
            data_loaded["BenchmarkProblems"][0][1]['BenchmarkFinalParameters'][0]['ProblemSizes'][0]['Exact'][0] = int(fullSize[0])
            data_loaded["BenchmarkProblems"][0][1]['BenchmarkFinalParameters'][0]['ProblemSizes'][0]['Exact'][1] = int(fullSize[1])
            data_loaded["BenchmarkProblems"][0][1]['BenchmarkFinalParameters'][0]['ProblemSizes'][0]['Exact'][2] = int(fullSize[2])
            data_loaded["BenchmarkProblems"][0][1]['BenchmarkFinalParameters'][0]['ProblemSizes'][0]['Exact'][3] = int(fullSize[3])
            print(data_loaded["BenchmarkProblems"][0][1]['BenchmarkFinalParameters'][0]['ProblemSizes'][0])
        except yaml.YAMLError as exc:
            print(exc)

        if isLSUenable == True:
            buildFolder = "/menghung/hcman/hipBLASLt/tensilelite/script/build"
            with io.open(sz+".yaml", 'w', encoding='utf8') as outfile:
                yaml.dump(data_loaded, outfile)
                test = subprocess.Popen(["rm","-rf",sz], stdout=subprocess.PIPE)
                output = test.communicate()[0]
                test = subprocess.Popen(["mkdir",sz], stdout=subprocess.PIPE)
                output = test.communicate()[0]
                f = open(sz+"/build.log", "w")
                test = subprocess.Popen(["/menghung/hcman/hipBLASLt/tensilelite/Tensile/bin/Tensile",sz+".yaml",buildFolder], stdout=f)
                output = test.communicate()[0]
                test = subprocess.Popen(["cp","-rf",buildFolder+"/3_LibraryLogic", sz], stdout=subprocess.PIPE)
                output = test.communicate()[0]
                test = subprocess.Popen(["rm","-rf",buildFolder+"/2_BenchmarkData"], stdout=subprocess.PIPE)
                output = test.communicate()[0]
                test = subprocess.Popen(["rm","-rf",buildFolder+"/3_LibraryLogic"], stdout=subprocess.PIPE)
                output = test.communicate()[0]
