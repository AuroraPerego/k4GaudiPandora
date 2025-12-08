/*
 * Copyright (c) 2020-2024 Key4hep-Project.
 *
 * This file is part of Key4hep.
 * See https://key4hep.github.io/key4hep-doc/ for further info.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 *  @file   DDMarlinPandora/src/DDExternalClusteringAlgorithm.cc
 *
 *  @brief  Implementation of the external clustering algorithm class.
 *
 *  $Log: $
 */

#include "edm4hep/CalorimeterHitCollection.h"
#include "edm4hep/ClusterCollection.h"

#include "DDExternalClusteringAlgorithm.h"
#include "DDPandoraPFANewAlgorithm.h"
#include "Helpers/XmlHelper.h"

// Pandora
#include "Api/PandoraContentApi.h"
#include "Pandora/PdgTable.h"

#include <Gaudi/Algorithm.h>

DDExternalClusteringAlgorithm::DDExternalClusteringAlgorithm() : m_flagClustersAsPhotons(false) {}

pandora::StatusCode DDExternalClusteringAlgorithm::Run() {
  try {
    const pandora::CaloHitList* pCaloHitList = NULL;
    PANDORA_RETURN_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentList(*this, pCaloHitList));

    if (pCaloHitList->empty())
      return pandora::STATUS_CODE_SUCCESS;

    const auto& gEvent = DDPandoraPFANewAlgorithm::GetCurrentEvent(&(this->GetPandora()));

    DataObject* rawObj = nullptr;
    StatusCode sc = gEvent.get()->retrieveObject("/Event/" + m_externalClusterCollectionNames, rawObj);
    if (sc.isFailure() || rawObj == nullptr) {
      throw std::runtime_error("Cannot retrieve external cluster collection");
    }

    auto anyWrapper = dynamic_cast<AnyDataWrapper<std::unique_ptr<podio::CollectionBase>>*>(rawObj);
    if (!anyWrapper) {
      throw std::runtime_error("Failed to cast to AnyDataWrapper");
    }

    // get the underlying CollectionBase*
    auto collBasePtr = anyWrapper->getData().get(); // CollectionBase*

    // now cast to your concrete collection type
    auto pExternalClusterCollection = dynamic_cast<edm4hep::ClusterCollection*>(collBasePtr);
    if (!pExternalClusterCollection) {
      throw std::runtime_error("Failed to cast CollectionBase to ClusterCollection");
    }
    const unsigned int nExternalClusters(pExternalClusterCollection->size());

    if (0 == nExternalClusters)
      return pandora::STATUS_CODE_SUCCESS;

    // Populate pandora parent address to calo hit map
    ExternalToPandoraCaloHitMap caloHitMap;

    for (pandora::CaloHitList::const_iterator hitIter = pCaloHitList->begin(), hitIterEnd = pCaloHitList->end();
         hitIter != hitIterEnd; ++hitIter) {
      const pandora::CaloHit* const pCaloHit = *hitIter;
      const edm4hep::CalorimeterHit* edmCaloHit =
          static_cast<const edm4hep::CalorimeterHit*>(pCaloHit->GetParentAddress());
      caloHitMap.emplace(edmCaloHit->getCellID(), pCaloHit);
    }

    // Recreate external clusters within the pandora framework
    const pandora::ClusterList* pClusterList = nullptr;
    std::string clusterListNameTmp = "ExternalClustersTmp";
    std::string clusterListNameFinal = "ExternalClusters";

    PANDORA_RETURN_RESULT_IF(
        pandora::STATUS_CODE_SUCCESS, !=,
        PandoraContentApi::CreateTemporaryListAndSetCurrent(*this, pClusterList, clusterListNameTmp));

    for (const edm4hep::Cluster& externalCluster : *pExternalClusterCollection) {

      const auto& calorimeterHitVec = externalCluster.getHits();

      pandora::CaloHitList pandoraHitList;

      for (const auto& edmHit : calorimeterHitVec) {
        auto itr = caloHitMap.find(edmHit.getCellID());
        if (itr == caloHitMap.end())
          continue;
        pandoraHitList.emplace_back(itr->second);
      }

      if (pandoraHitList.empty())
        continue;

      object_creation::ClusterParameters clusterParameters;
      clusterParameters.m_caloHitList = pandoraHitList;

      const pandora::Cluster* pPandoraCluster = nullptr;

      PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=,
                              PandoraContentApi::Cluster::Create(*this, clusterParameters, pPandoraCluster));

      if (m_flagClustersAsPhotons) {
        PandoraContentApi::Cluster::Metadata metadata;
        metadata.m_particleId = pandora::PHOTON;
        PANDORA_RETURN_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=,
                                 PandoraContentApi::Cluster::AlterMetadata(*this, pPandoraCluster, metadata));
      }
    }

    if (!pClusterList->empty()) {
      PANDORA_RETURN_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=,
                               PandoraContentApi::SaveList<pandora::Cluster>(*this, clusterListNameFinal));
      PANDORA_RETURN_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=,
                               PandoraContentApi::ReplaceCurrentList<pandora::Cluster>(*this, clusterListNameFinal));
    }

  } catch (pandora::StatusCodeException& statusCodeException) {
    return statusCodeException.GetStatusCode();
  } catch (std::exception& exception) {
    std::cout << "DDExternalClusteringAlgorithm failure: " << exception.what() << std::endl;
    return pandora::STATUS_CODE_FAILURE;
  }

  return pandora::STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

pandora::StatusCode DDExternalClusteringAlgorithm::ReadSettings(const pandora::TiXmlHandle xmlHandle) {
  PANDORA_RETURN_RESULT_IF(
      pandora::STATUS_CODE_SUCCESS, !=,
      pandora::XmlHelper::ReadValue(xmlHandle, "ExternalClusterCollectionName", m_externalClusterCollectionNames));

  PANDORA_RETURN_RESULT_IF_AND_IF(
      pandora::STATUS_CODE_SUCCESS, pandora::STATUS_CODE_NOT_FOUND, !=,
      pandora::XmlHelper::ReadValue(xmlHandle, "FlagClustersAsPhotons", m_flagClustersAsPhotons));

  return pandora::STATUS_CODE_SUCCESS;
}
