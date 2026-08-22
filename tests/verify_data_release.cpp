#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <json/json.h>

#include "environmental_grib/xtd_package.h"

namespace eg = environmental_grib;

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: xtidal_verify_data_release PACKAGE.xtdt\n";
    return 2;
  }
  try {
    const std::filesystem::path package = argv[1];
    Require(package.extension() == ".xtdt",
            "OfflineTides release data must use .xtdt");

    eg::XtdPackageReader reader(package);
    const auto& status = reader.status();
    Require(status.authenticated, "package is not authenticated");
    Require(status.format_version == 2, "package is not authenticated XTD v2");
    Require(status.height_available, "water-level harmonics are unavailable");
    Require(status.height_quality_available, "water-level quality is unavailable");
    Require(status.vertical_datum_available,
            "Chart Datum transformation is unavailable");
    Require(!status.tide_available,
            "OfflineTides height release must not contain a current field");
    Require(status.metadata["package_profile"].asString() == "xtidal-tide-v1",
            "unexpected package profile");
    Require(status.metadata["recommended_extension"].asString() == ".xtdt",
            "unexpected recommended extension");
    Require(status.metadata["dataset_id"].asString() ==
                "offlinetides-global-v1.0.0-alpha1",
            "unexpected OfflineTides dataset identity");
    Require(status.metadata["dataset_name"].asString() ==
                "OfflineTides global observation-constrained harmonics",
            "unexpected OfflineTides dataset name");

    Json::Value evidence = reader.VerifyAllComponents();
    evidence["release_gate"] = "passed";
    evidence["dataset_id"] = status.metadata["dataset_id"];
    evidence["dataset_name"] = status.metadata["dataset_name"];
    evidence["package_id"] = status.package_id;
    evidence["package_hash"] = status.package_hash;
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    std::cout << Json::writeString(writer, evidence);
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
