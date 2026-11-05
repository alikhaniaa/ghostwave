#include "electrosense.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string>

namespace e=ghost::electro;
namespace fs=std::filesystem;
namespace {
constexpr std::size_t sweepPoints=33;
constexpr std::size_t envelopeSamples=257;
constexpr double sphereRadiusM=0.0015;
constexpr double materialDistanceM=0.12;
constexpr std::uint32_t defaultSeed=5489;
struct Options { fs::path outputDirectory="field-lab";std::uint32_t seed=defaultSeed;bool help{}; };

Options parseOptions(int argc,char** argv) {
    Options options;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--help" || arg=="-h")options.help=true;
        else if(arg=="--output-dir" || arg=="--seed") {
            if(++i>=argc)throw std::invalid_argument("Missing value after "+arg);
            const std::string value=argv[i];
            if(arg=="--output-dir") {
                if(value.empty())throw std::invalid_argument("Output directory cannot be empty");
                options.outputDirectory=value;
            } else {
                if(value.empty() || value.find_first_not_of("0123456789")!=std::string::npos)
                    throw std::invalid_argument("Seed must be an unsigned decimal 32-bit integer");
                std::size_t consumed=0;
                const auto seed=std::stoull(value,&consumed);
                if(consumed!=value.size() || seed>std::numeric_limits<std::uint32_t>::max())
                    throw std::invalid_argument("Seed is outside the unsigned 32-bit range");
                options.seed=static_cast<std::uint32_t>(seed);
            }
        } else throw std::invalid_argument("Unknown option: "+arg);
    }
    return options;
}

std::ofstream outputFile(const fs::path& path) {
    // Binary mode fixes line endings; classic locale fixes decimal separators.
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);
    stream.imbue(std::locale::classic());
    stream<<std::setprecision(std::numeric_limits<double>::max_digits10);
    if(!stream)throw std::runtime_error("Cannot create output file: "+path.string());
    return stream;
}
void finish(std::ofstream& stream,const fs::path& path) {
    stream.flush();
    if(!stream)throw std::runtime_error("Writing failed: "+path.string());
    stream.close();
    if(stream.fail())throw std::runtime_error("Closing output failed: "+path.string());
}
double relativeError(e::Complex actual,e::Complex expected) {
    const double scale=std::abs(expected);
    return scale>0?std::abs(actual-expected)/scale:std::abs(actual);
}
void verify(bool value,const char* message) {
    if(!value)throw std::runtime_error(std::string("Analytical verification failed: ")+message);
}
double logPoint(double low,double high,std::size_t i) {
    if(i==0)return low;
    if(i+1==sweepPoints)return high;
    return low*std::pow(high/low,static_cast<double>(i)/(sweepPoints-1));
}
double seededPhase(std::uint32_t seed) {
    // Explicit integer recurrence; no implementation-dependent random distribution.
    const std::uint32_t state=seed*1664525u+1013904223u;
    return 2*e::pi*(static_cast<double>(state)/4294967296.0);
}
void passiveHeader(std::ofstream& csv) {
    csv<<"experiment,seed,index,distance_m,frequency_hz,water_conductivity_s_per_m,water_relative_permittivity,"
          "object_conductivity_s_per_m,object_relative_permittivity,target_radius_m,source_moment_a_m,"
          "incident_field_peak_v_per_m,contrast_real_dimensionless,contrast_imag_dimensionless,"
          "return_potential_real_v,return_potential_imag_v,return_potential_peak_v,return_potential_phase_rad,"
          "return_phase_available,return_potential_rms_v,expected_return_potential_peak_v,"
          "relative_error_dimensionless,size_distance_ratio_dimensionless,approximation_limited\n";
}

void passiveRow(std::ofstream& csv,const char* experiment,std::uint32_t seed,std::size_t index,
                double distanceM,double frequencyHz,e::Medium water,e::Material material) {
    const e::Dipole source; // Fixed 2e-6 A*m along +X, phase zero, at origin.
    const e::Sphere sphere{{distanceM,0,0},sphereRadiusM,material};
    const auto response=e::sphereResponse(source,sphere,water,frequencyHz);
    // Only the SECONDARY potential is read at the source position. The singular
    // primary point-source potential there is neither sampled nor interpreted.
    const auto sample=e::sampleSphere(response,{});
    const auto gamma=e::admittivity(water,frequencyHz);
    const auto expected=-2.0*source.momentAm.x*std::pow(sphereRadiusM,3)*response.contrast/
                         (4.0*e::pi*gamma*std::pow(distanceM,5));
    const double error=relativeError(sample.potentialV,expected);
    verify(std::isfinite(error)&&error<1e-11,"isolated collinear sphere return");
    const double amplitude=std::abs(sample.potentialV);
    const bool phaseAvailable=amplitude>0;
    const bool limited=response.sourceCoreOverlap || response.sizeDistanceRatio>0.2 || sample.regularized;
    csv<<experiment<<','<<seed<<','<<index<<','<<distanceM<<','<<frequencyHz<<','
       <<water.conductivitySm<<','<<water.relativePermittivity<<','<<material.conductivitySm<<','
       <<material.relativePermittivity<<','<<sphereRadiusM<<','<<source.momentAm.x<<','
       <<e::norm(response.incidentFieldVm)<<','<<response.contrast.real()<<','<<response.contrast.imag()<<','
       <<sample.potentialV.real()<<','<<sample.potentialV.imag()<<','<<amplitude<<','
       <<(phaseAvailable?std::arg(sample.potentialV):0)<<','<<phaseAvailable<<','<<amplitude/std::sqrt(2.0)<<','
       <<std::abs(expected)<<','<<error<<','<<response.sizeDistanceRatio<<','<<limited<<'\n';
}

void distanceSweep(const Options& options) {
    const auto path=options.outputDirectory/"distance.csv";
    auto csv=outputFile(path);passiveHeader(csv);
    for(std::size_t i=0;i<sweepPoints;++i)
        passiveRow(csv,"isolated_sphere_distance",options.seed,i,logPoint(0.03,1.2,i),
                   e::defaultFrequencyHz,{0.01,0},{0.03,0});
    finish(csv,path);
}
void conductivitySweep(const Options& options) {
    const auto path=options.outputDirectory/"conductivity.csv";
    auto csv=outputFile(path);passiveHeader(csv);
    constexpr std::array<double,13> conductivity{{0.001,0.002,0.003,0.004,0.006,0.008,
                                                 0.01,0.014,0.018,0.024,0.03,0.05,0.1}};
    for(std::size_t i=0;i<conductivity.size();++i)
        passiveRow(csv,"isolated_sphere_water_conductivity",options.seed,i,materialDistanceM,
                   e::defaultFrequencyHz,{conductivity[i],80},{0.03,80});
    finish(csv,path);
}
void materialFrequencySweep(const Options& options) {
    const auto path=options.outputDirectory/"material_frequency.csv";
    auto csv=outputFile(path);passiveHeader(csv);
    for(std::size_t i=0;i<sweepPoints;++i)
        passiveRow(csv,"isolated_sphere_material_frequency",options.seed,i,materialDistanceM,
                   logPoint(10,10000,i),{0.01,80},{0.01,100000});
    finish(csv,path);
}

void interferenceSweep(const Options& options) {
    const auto path=options.outputDirectory/"interference.csv";
    auto csv=outputFile(path);
    csv<<"experiment,seed,case_index,sample_index,time_s,carrier_frequency_hz,other_frequency_hz,"
          "difference_frequency_hz,beat_frequency_hz,water_conductivity_s_per_m,water_relative_permittivity,"
          "source_moment_a_m,other_moment_a_m,initial_other_source_phase_rad,own_probe_peak_v,other_probe_peak_v,"
          "mixed_envelope_peak_v,mixed_narrowband_rms_v,envelope_amplitude_change_fraction,"
          "relative_envelope_phase_rad,modulation_depth_dimensionless,expected_envelope_peak_v,relative_error_dimensionless\n";
    const e::Medium water{0.01,80};
    const e::Vec3 electrode{0.03,0.015,0},reference{};
    const e::Dipole own{{-0.07,0,0},{2e-6,0,0},0,0.002};
    const e::Dipole other{{0.35,0.10,0.02},{2e-6,0,0},seededPhase(options.seed),0.002};
    constexpr std::array<double,9> differences{{-40,-20,-10,-4,0,4,10,20,40}};
    const auto baseline=e::dipolePotential(own,electrode,water,e::defaultFrequencyHz)-
                        e::dipolePotential(own,reference,water,e::defaultFrequencyHz);
    for(std::size_t c=0;c<differences.size();++c) {
        const double otherHz=e::defaultFrequencyHz+differences[c];
        const auto perturbation=e::dipolePotential(other,electrode,water,otherHz)-
                                e::dipolePotential(other,reference,water,otherHz);
        const double a=std::abs(baseline),b=std::abs(perturbation);
        for(std::size_t i=0;i<envelopeSamples;++i) {
            const double time=static_cast<double>(i)/(envelopeSamples-1);
            const auto mixed=e::mixSignals({{baseline,e::defaultFrequencyHz},{perturbation,otherHz}},
                                          e::defaultFrequencyHz,time);
            const double angle=2*e::pi*differences[c]*time+std::arg(perturbation)-std::arg(baseline);
            const double expected=std::sqrt(std::max(0.0,a*a+b*b+2*a*b*std::cos(angle)));
            const double error=relativeError({mixed.envelopeV,0},{expected,0});
            verify(std::isfinite(error)&&error<1e-11,"two-tone envelope identity");
            csv<<"two_wave_emitter_envelope,"<<options.seed<<','<<c<<','<<i<<','<<time<<','
               <<e::defaultFrequencyHz<<','<<otherHz<<','<<differences[c]<<','
               <<e::beatFrequencyHz(e::defaultFrequencyHz,otherHz)<<','<<water.conductivitySm<<','
               <<water.relativePermittivity<<','<<own.momentAm.x<<','<<other.momentAm.x<<','<<other.phaseRad<<','
               <<a<<','<<b<<','<<mixed.envelopeV<<','<<mixed.narrowbandRmsV<<','<<(mixed.envelopeV-a)/a<<','
               <<std::arg(mixed.basebandV/baseline)<<','<<e::twoToneModulationDepth(a,b)<<','<<expected<<','<<error<<'\n';
        }
    }
    finish(csv,path);
}

void manifest(const Options& options) {
    const auto path=options.outputDirectory/"manifest.json";
    auto file=outputFile(path);
    file<<"{\n  \"schema_version\": 1,\n  \"seed\": "<<options.seed
        <<",\n  \"phase_convention\": \"peak phasors, exp(+i 2 pi f t)\",\n"
          "  \"seed_role\": \"initial relative emitter phase only; no added noise\",\n"
          "  \"distance_geometry\": \"source at origin along +X; sphere centre on +X; secondary potential receiver at origin\",\n"
          "  \"sphere_radius_m\": "<<sphereRadiusM<<",\n"
          "  \"material_sweep_distance_m\": "<<materialDistanceM<<",\n"
          "  \"source_moment_a_m\": 0.000002,\n  \"source_core_radius_m\": 0.002,\n"
          "  \"interference_source_m\": [-0.07, 0, 0],\n"
          "  \"interference_other_source_m\": [0.35, 0.10, 0.02],\n"
          "  \"interference_electrode_m\": [0.03, 0.015, 0],\n"
          "  \"interference_reference_m\": [0, 0, 0],\n"
          "  \"interference_initial_other_phase_rad\": "<<seededPhase(options.seed)<<",\n"
          "  \"interference_sample_rate_hz\": 256,\n"
          "  \"interference_duration_s\": 1,\n"
          "  \"interference_sample_kind\": \"analytic envelope only; not a sampled 720 Hz carrier waveform\",\n"
          "  \"rows\": {\"distance.csv\": 33, \"conductivity.csv\": 13, \"material_frequency.csv\": 33, \"interference.csv\": 2313},\n"
          "  \"analytical_relative_error_limit\": 1e-11,\n"
          "  \"limits\": [\"isolated sphere in a locally uniform incident field\","
          " \"homogeneous unbounded water\", \"no fish body or skin boundary solver\","
          " \"no neural detection threshold or physiological calibration\","
          " \"material frequency sweep extends outside brown-ghost baseline EOD range\","
          " \"approximation_limited=false is a geometric guard, not empirical validation\"],\n"
          "  \"reproducibility\": \"same executable and seed produce identical files; libm and compiler rounding can differ across platforms\"\n}\n";
    finish(file,path);
}
} // namespace

int main(int argc,char** argv) {
    try {
        const auto options=parseOptions(argc,argv);
        if(options.help) {
            std::cout<<"field_lab [--output-dir DIRECTORY] [--seed UINT32]\n"
                        "Writes distance.csv, conductivity.csv, material_frequency.csv, interference.csv, and manifest.json.\n"
                        "Default output: field-lab; default seed: 5489. Existing files with these names are replaced.\n"
                        "Seed controls only initial emitter phase. No noise or graphics are used.\n"
                        "Each row is checked against an independent analytical limit; failures return a nonzero exit code.\n";
            return 0;
        }
        fs::create_directories(options.outputDirectory);
        distanceSweep(options);conductivitySweep(options);materialFrequencySweep(options);
        interferenceSweep(options);manifest(options);
        std::cout<<"Field laboratory: 2392 rows verified and written to "<<fs::absolute(options.outputDirectory).string()
                 <<"; seed "<<options.seed<<". Isolated-object/probe model; no physiological calibration.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"field_lab: "<<error.what()<<'\n';
        return 1;
    }
}
