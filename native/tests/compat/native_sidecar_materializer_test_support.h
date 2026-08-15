#pragma once

#include "traceloom/ir/native_ir.h"

#include <string>

namespace traceloom::testing::sidecar_materializer {

std::string temp_db_path();
int run_scalar_int(const std::string& path, const std::string& sql);
std::string run_scalar_text(const std::string& path, const std::string& sql);
void run_sql(const std::string& path, const std::string& sql);

NativeIr build_collective_repeat_ir();
NativeIr build_exact_cuda_graph_replay_ir();

void run_graph_materializer_tests();
void run_packaging_materializer_tests();

}  // namespace traceloom::testing::sidecar_materializer
