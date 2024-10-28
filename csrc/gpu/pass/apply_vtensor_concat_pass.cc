// Copyright (c) 2024 PaddlePaddle Authors. All Rights Reserved.
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

#include <iostream>

#include "paddle/extension.h"

namespace {

class ApplyVTensorConcatPattern : public paddle::drr::DrrPatternBase {
public:
  std::string name() const override { return "ApplyVTensorConcatPattern"; }

  void operator()(paddle::drr::DrrPatternContext *ctx) const override {
    paddle::drr::SourcePattern pat = ctx->SourcePattern();

    const auto &full = pat.Op("pd_op.full", {{"value", pat.Attr("axis")}});
    pat.Tensor("concat_axis") = full();

    const auto &concat_combine = pat.Op("builtin.combine");
    pat.Tensor("concat_in") =
        concat_combine(pat.Tensor("concat_in1"), pat.Tensor("concat_in2"));

    const auto &concat = pat.Op("pd_op.concat");
    pat.Tensor("concat_out") =
        concat(pat.Tensor("concat_in"), pat.Tensor("concat_axis"));

    pat.AddConstraint([](const paddle::drr::MatchContext &match_ctx) {
      auto shape1 = pir::GetShapeFromValue(match_ctx.Tensor("concat_in1"));
      auto shape2 = pir::GetShapeFromValue(match_ctx.Tensor("concat_in2"));
      auto matched = (match_ctx.Attr<double>("axis") == 1.0 &&
                      shape1.size() == 4 && shape2.size() == 4 && true);

      if (matched) {
        bool has_yield = false, has_attn = false;
        auto out = match_ctx.Tensor("concat_out");
        for (auto op = out.use_begin(); op != out.use_end(); ++op) {
          auto name = op->owner()->name();
          has_yield |= name == "cf.yield";
          has_attn |= name == "pd_op.flash_attn";
        }
        matched &= has_yield;
      }

      return matched;
    });

    paddle::drr::ResultPattern res = pat.ResultPattern();
    const auto &vconcat = res.Op("custom_op.vtensor_reserve_one_token",
                                 {{"transposed_input", res.BoolAttr(false)}});
    res.Tensor("concat_out") =
        vconcat(res.Tensor("concat_in1"), res.Tensor("concat_in2"));
  }
};

class ApplyVTensorConcatPass : public pir::PatternRewritePass {
public:
  ApplyVTensorConcatPass()
      : pir::PatternRewritePass("apply_vtensor_concat_pass", 2) {}

  pir::RewritePatternSet InitializePatterns(pir::IrContext *context) override {
    pir::RewritePatternSet ps(context);
    ps.Add(paddle::drr::Create<ApplyVTensorConcatPattern>(context));
    return ps;
  }
};

}  // namespace

REGISTER_IR_PASS(apply_vtensor_concat_pass, ApplyVTensorConcatPass);