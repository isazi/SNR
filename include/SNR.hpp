// Copyright 2014 Alessio Sclocco <a.sclocco@vu.nl>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>
#include <cmath>

#include <utils.hpp>
#include <Observation.hpp>


#ifndef SNR_HPP
#define SNR_HPP

namespace PulsarSearch {

// Sequential SNR
template< typename T > void snrDedispersedTS(const unsigned int second, const AstroData::Observation & observation, const std::vector< T > & dedispersedTS, std::vector< T > & maxS, std::vector< float > & meanS, std::vector< float > & rmsS);
template< typename T > void snrFoldedTS(const AstroData::Observation & observation, const std::vector< T > & foldedTS, std::vector< T > & snrs);
// OpenCL SNR
std::string * getSNRDedispersedOpenCL(const unsigned int nrDMsPerBlock, const unsigned int nrDMsPerThread, const std::string & dataType, const AstroData::Observation & observation);
std::string * getSNRFoldedOpenCL(const unsigned int nrDMsPerBlock, const unsigned int nrPeriodsPerBlock, const unsigned int nrDMsPerThread, const unsigned int nrPeriodsPerThread, const std::string & dataType, const AstroData::Observation & observation);


// Implementations
template< typename T > void snrDedispersedTS(const unsigned int second, const AstroData::Observation & observation, const std::vector< T > & dedispersedTS, std::vector< T > & maxS, std::vector< float > & meanS, std::vector< float > & rmsS) {
  for ( unsigned int dm = 0; dm < observation.getNrDMs(); dm++ ) {
    T max = 0;
    float mean = 0.0f;
    float rms = 0.0f;

    for ( unsigned int sample = 0; sample < observation.getNrSamplesPerSecond(); sample++ ) {
      T value = dedispersedTS[(dm * observation.getNrSamplesPerPaddedSecond()) + sample];

      mean += value;
      rms += (value * value);

      if ( value > max ) {
        max = value;
      }
    }

    if ( max > maxS[dm] ) {
      maxS[dm] = max;
    }
    meanS[dm] = ((meanS[dm] * observation.getNrSamplesPerSecond() * second) + mean) / (observation.getNrSamplesPerSecond() * (second + 1));
    rmsS[dm] = ((rmsS[dm] * observation.getNrSamplesPerSecond() * second) + rms) / (observation.getNrSamplesPerSecond() * (second + 1));
  }
}

template< typename T > void snrFoldedTS(AstroData::Observation & observation, const std::vector< T > & foldedTS, std::vector< T > & snrs) {
  for ( unsigned int period = 0; period < observation.getNrPeriods(); period++ ) {
    for ( unsigned int dm = 0; dm < observation.getNrDMs(); dm++ ) {
			T max = 0;
			float average = 0.0f;
			float rms = 0.0f;

			for ( unsigned int bin = 0; bin < observation.getNrBins(); bin++ ) {
				T value = foldedTS[(bin * observation.getNrPeriods() * observation.getNrPaddedDMs()) + (period * observation.getNrPaddedDMs()) + dm];

				average += value;
				rms += (value * value);

				if ( value > max ) {
					max = value;
				}
			}
			average /= observation.getNrBins();
			rms = std::sqrt(rms / observation.getNrBins());

			snrs[(period * observation.getNrPaddedDMs()) + dm] = (max - average) / rms;
		}
	}
}

std::string * getSNRDedispersedOpenCL(const unsigned int nrDMsPerBlock, const unsigned int nrDMsPerThread, const std::string & dataType, const AstroData::Observation & observation) {
  std::string * code = new std::string();

  // Begin kernel's template
  *code = "__kernel void snrDedispersed(const float second, __global const " + dataType + " * const restrict dedispersedData, __global " + dataType + " * const restrict maxS, __global float * const restrict meanS, __global float * const restrict rmsS) {\n"
    "<%DEF_DM%>"
    + dataType + " globalItem = 0;\n"
    "\n"
    "for ( unsigned int sample = 0; sample < " + isa::utils::toString< unsigned int >(observation.getNrSamplesPerSecond()) + "; sample++ ) {\n"
    "<%COMPUTE_DM%>"
    "}\n"
    "<%STORE_DM%>"
    "}\n";
  std::string defDMTemplate = "const unsigned int dm<%DM_NUM%> = (get_group_id(0) * " + isa::utils::toString< unsigned int >(nrDMsPerBlock * nrDMsPerThread) + ") + get_local_id(0) + <%OFFSET%>;\n"
    + dataType + " meanDM<%DM_NUM%> = 0;\n"
    + dataType + " rmsDM<%DM_NUM%> = 0;\n"
    + dataType + " maxDM<%DM_NUM%> = 0;\n";
  std::string computeDMTemplate = "globalItem = dedispersedData[(sample * " + isa::utils::toString< unsigned int >(observation.getNrPaddedDMs()) + ") + dm<%DM_NUM%>];\n"
    "meanDM<%DM_NUM%> += globalItem;\n"
    "rmsDM<%DM_NUM%> += (globalItem * globalItem);\n"
    "maxDM<%DM_NUM%> = fmax(maxDM<%DM_NUM%>, globalItem);\n";
  std::string storeDMTemplate = "maxS[dm<%DM_NUM%>] = fmax(maxS[dm<%DM_NUM%>], maxDM<%DM_NUM%>);\n"
    "meanS[dm<%DM_NUM%>] = ((meanS[dm<%DM_NUM%>] * " + isa::utils::toString< unsigned int >(observation.getNrSamplesPerSecond()) + ".0f * second) + meanDM<%DM_NUM%>) / (" + isa::utils::toString< unsigned int >(observation.getNrSamplesPerSecond()) + ".0f * (second + 1.0f));\n"
    "rmsS[dm<%DM_NUM%>] = ((rmsS[dm<%DM_NUM%>] * " + isa::utils::toString< unsigned int >(observation.getNrSamplesPerSecond()) + ".0f * second) + rmsDM<%DM_NUM%>) / (" + isa::utils::toString< unsigned int >(observation.getNrSamplesPerSecond()) + ".0f * (second + 1.0f));\n";
  // End kernel's template

  std::string * defDM_s = new std::string();
  std::string * computeDM_s = new std::string();
  std::string * storeDM_s = new std::string();

  for ( unsigned int dm = 0; dm < nrDMsPerThread; dm++ ) {
    std::string dm_s = isa::utils::toString< unsigned int >(dm);
    std::string offset_s = isa::utils::toString< unsigned int >(dm * nrDMsPerBlock);
    std::string * temp_s = 0;

    temp_s = isa::utils::replace(&defDMTemplate, "<%DM_NUM%>", dm_s);
    temp_s = isa::utils::replace(temp_s, "<%OFFSET%>", offset_s, true);
    defDM_s->append(*temp_s);
    delete temp_s;
    temp_s = isa::utils::replace(&computeDMTemplate, "<%DM_NUM%>", dm_s);
    computeDM_s->append(*temp_s);
    delete temp_s;
    temp_s = isa::utils::replace(&storeDMTemplate, "<%DM_NUM%>", dm_s);
    storeDM_s->append(*temp_s);
    delete temp_s;
  }

  code = isa::utils::replace(code, "<%DEF_DM%>", *defDM_s, true);
  delete defDM_s;
  code = isa::utils::replace(code, "<%COMPUTE_DM%>", *computeDM_s, true);
  delete computeDM_s;
  code = isa::utils::replace(code, "<%STORE_DM%>", *storeDM_s, true);
  delete storeDM_s;

  return code;
}

std::string * getSNRFoldedOpenCL(const unsigned int nrDMsPerBlock, const unsigned int nrPeriodsPerBlock, const unsigned int nrDMsPerThread, const unsigned int nrPeriodsPerThread, const std::string & dataType, const AstroData::Observation & observation) {
  std::string * code = new std::string();

  // Begin kernel's template
  std::string nrPaddedDMs_s = isa::utils::toString< unsigned int >(observation.getNrPaddedDMs());
  std::string nrBinsInverse_s = isa::utils::toString< float >(1.0f / observation.getNrBins());

  *code = "__kernel void snrFolded(__global const " + dataType + " * const restrict foldedData, __global " + dataType + " * const restrict snrs) {\n"
    + dataType + " globalItem = 0;\n"
    "<%DEF_DM%>"
    "<%DEF_PERIOD%>"
    "<%DEF_DM_PERIOD%>"
    "\n"
    "for ( unsigned int bin = 0; bin < " + isa::utils::toString< unsigned int >(observation.getNrBins()) + "; bin++ ) {\n"
    "<%COMPUTE%>"
    "}\n"
    "<%STORE%>"
    "}\n";
    std::string defDMsTemplate = "const unsigned int dm<%DM_NUM%> = (get_group_id(0) * " + isa::utils::toString< unsigned int >(nrDMsPerBlock * nrDMsPerThread) + ") + get_local_id(0) + <%DM_OFFSET%>;\n";
  std::string defPeriodsTemplate = "const unsigned int period<%PERIOD_NUM%> = (get_group_id(1) * " + isa::utils::toString< unsigned int >(nrPeriodsPerBlock * nrPeriodsPerThread) + ") + get_local_id(1) + <%PERIOD_OFFSET%>;\n";
  std::string defDMsPeriodsTemplate = dataType + " averageDM<%DM_NUM%>p<%PERIOD_NUM%> = 0;\n"
    + dataType + " rmsDM<%DM_NUM%>p<%PERIOD_NUM%> = 0;\n"
    + dataType + " maxDM<%DM_NUM%>p<%PERIOD_NUM%> = 0;\n";
  std::string computeTemplate = "globalItem = foldedData[(bin * " + isa::utils::toString(observation.getNrPeriods() * observation.getNrPaddedDMs()) + ") + (period<%PERIOD_NUM%> * " + nrPaddedDMs_s + ") + dm<%DM_NUM%>];\n"
    "averageDM<%DM_NUM%>p<%PERIOD_NUM%> += globalItem;\n"
    "rmsDM<%DM_NUM%>p<%PERIOD_NUM%> += (globalItem * globalItem);\n"
    "maxDM<%DM_NUM%>p<%PERIOD_NUM%> = fmax(maxDM<%DM_NUM%>p<%PERIOD_NUM%>, globalItem);\n";
  std::string storeTemplate = "averageDM<%DM_NUM%>p<%PERIOD_NUM%> *= " + nrBinsInverse_s + "f;\n"
    "rmsDM<%DM_NUM%>p<%PERIOD_NUM%> *= " + nrBinsInverse_s + "f;\n"
    "snrs[(period<%PERIOD_NUM%> * " + nrPaddedDMs_s + ") + dm<%DM_NUM%>] = (maxDM<%DM_NUM%>p<%PERIOD_NUM%> - averageDM<%DM_NUM%>p<%PERIOD_NUM%>) / native_sqrt(rmsDM<%DM_NUM%>p<%PERIOD_NUM%>);\n";
  // End kernel's template

  std::string * defDM_s = new std::string();
  std::string * defPeriod_s = new std::string();
  std::string * defDMPeriod_s = new std::string();
  std::string * compute_s = new std::string();
  std::string * store_s = new std::string();

  for ( unsigned int dm = 0; dm < nrDMsPerThread; dm++ ) {
    std::string dm_s = isa::utils::toString< unsigned int >(dm);
    std::string * temp_s = 0;

    temp_s = isa::utils::replace(&defDMsTemplate, "<%DM_NUM%>", dm_s);
    if ( dm == 0 ) {
      std::string empty_s;
      temp_s = isa::utils::replace(temp_s, " + <%DM_OFFSET%>", empty_s, true);
    } else {
      std::string offset_s = isa::utils::toString(dm * nrDMsPerBlock);
      temp_s = isa::utils::replace(temp_s, "<%DM_OFFSET%>", offset_s, true);
    }
    defDM_s->append(*temp_s);
    delete temp_s;
  }
  for ( unsigned int period = 0; period < nrPeriodsPerThread; period++ ) {
    std::string period_s = isa::utils::toString< unsigned int >(period);
    std::string * temp_s = 0;

    temp_s = isa::utils::replace(&defPeriodsTemplate, "<%PERIOD_NUM%>", period_s);
    if ( period == 0 ) {
      std::string empty_s;
      temp_s = isa::utils::replace(temp_s, " + <%PERIOD_OFFSET%>", empty_s, true);
    } else {
      std::string offset_s = isa::utils::toString(period * nrPeriodsPerBlock);
      temp_s = isa::utils::replace(temp_s, "<%PERIOD_OFFSET%>", offset_s, true);
    }
    defPeriod_s->append(*temp_s);
    delete temp_s;

    for ( unsigned int dm = 0; dm < nrDMsPerThread; dm++ ) {
      std::string dm_s = isa::utils::toString< unsigned int >(dm);

      temp_s = isa::utils::replace(&defDMsPeriodsTemplate, "<%DM_NUM%>", dm_s);
      temp_s = isa::utils::replace(temp_s, "<%PERIOD_NUM%>", period_s, true);
      defDMPeriod_s->append(*temp_s);
      delete temp_s;
      temp_s = isa::utils::replace(&computeTemplate, "<%DM_NUM%>", dm_s);
      temp_s = isa::utils::replace(temp_s, "<%PERIOD_NUM%>", period_s, true);
      compute_s->append(*temp_s);
      delete temp_s;
      temp_s = isa::utils::replace(&storeTemplate, "<%DM_NUM%>", dm_s);
      temp_s = isa::utils::replace(temp_s, "<%PERIOD_NUM%>", period_s, true);
      store_s->append(*temp_s);
      delete temp_s;
    }
  }

  code = isa::utils::replace(code, "<%DEF_DM%>", *defDM_s, true);
  delete defDM_s;
  code = isa::utils::replace(code, "<%DEF_PERIOD%>", *defPeriod_s, true);
  delete defPeriod_s;
  code = isa::utils::replace(code, "<%DEF_DM_PERIOD%>", *defDMPeriod_s, true);
  delete defDMPeriod_s;
  code = isa::utils::replace(code, "<%COMPUTE%>", *compute_s, true);
  delete compute_s;
  code = isa::utils::replace(code, "<%STORE%>", *store_s, true);
  delete store_s;

  return code;
}

} // PulsarSearch

#endif // SNR_HPP

