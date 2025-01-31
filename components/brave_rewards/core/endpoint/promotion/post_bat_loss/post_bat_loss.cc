/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_rewards/core/endpoint/promotion/post_bat_loss/post_bat_loss.h"

#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "brave/components/brave_rewards/core/common/environment_config.h"
#include "brave/components/brave_rewards/core/common/request_signer.h"
#include "brave/components/brave_rewards/core/common/url_helpers.h"
#include "brave/components/brave_rewards/core/common/url_loader.h"
#include "brave/components/brave_rewards/core/rewards_engine_impl.h"
#include "brave/components/brave_rewards/core/wallet/wallet.h"
#include "net/http/http_status_code.h"

namespace brave_rewards::internal {
namespace endpoint {
namespace promotion {

PostBatLoss::PostBatLoss(RewardsEngineImpl& engine) : engine_(engine) {}

PostBatLoss::~PostBatLoss() = default;

std::string PostBatLoss::GetUrl(const int32_t version) {
  const auto wallet = engine_->wallet()->GetWallet();
  if (!wallet) {
    engine_->LogError(FROM_HERE) << "Wallet is null";
    return "";
  }

  auto url =
      URLHelpers::Resolve(engine_->Get<EnvironmentConfig>().rewards_grant_url(),
                          {"/v1/wallets/", wallet->payment_id,
                           "/events/batloss/", base::NumberToString(version)});
  return url.spec();
}

std::string PostBatLoss::GeneratePayload(const double amount) {
  return base::StringPrintf(R"({"amount": %f})", amount);
}

mojom::Result PostBatLoss::CheckStatusCode(const int status_code) {
  if (status_code == net::HTTP_INTERNAL_SERVER_ERROR) {
    engine_->LogError(FROM_HERE) << "Internal server error";
    return mojom::Result::FAILED;
  }

  if (status_code != net::HTTP_OK) {
    engine_->LogError(FROM_HERE) << "Unexpected HTTP status: " << status_code;
    return mojom::Result::FAILED;
  }

  return mojom::Result::OK;
}

void PostBatLoss::Request(const double amount,
                          const int32_t version,
                          PostBatLossCallback callback) {
  const auto wallet = engine_->wallet()->GetWallet();
  if (!wallet) {
    std::move(callback).Run(mojom::Result::FAILED);
    return;
  }

  const std::string payload = GeneratePayload(amount);

  auto request = mojom::UrlRequest::New();
  request->url = GetUrl(version);
  request->content = payload;
  request->content_type = "application/json; charset=utf-8";
  request->method = mojom::UrlMethod::POST;

  auto signer = RequestSigner::FromRewardsWallet(*wallet);
  if (!signer || !signer->SignRequest(*request)) {
    engine_->LogError(FROM_HERE) << "Unable to sign request";
    std::move(callback).Run(mojom::Result::FAILED);
    return;
  }

  engine_->Get<URLLoader>().Load(
      std::move(request), URLLoader::LogLevel::kDetailed,
      base::BindOnce(&PostBatLoss::OnRequest, base::Unretained(this),
                     std::move(callback)));
}

void PostBatLoss::OnRequest(PostBatLossCallback callback,
                            mojom::UrlResponsePtr response) {
  DCHECK(response);
  std::move(callback).Run(CheckStatusCode(response->status_code));
}

}  // namespace promotion
}  // namespace endpoint
}  // namespace brave_rewards::internal
