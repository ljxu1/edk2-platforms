/** @file
  This file is PeiSiPolicyLib library creates default settings of RC
  Policy and installs RC Policy PPI.

Copyright (c) 2017, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "PeiSiPolicyLibrary.h"


/**
  SiCreatePreMemConfigBlocks creates the config blocksg of Silicon PREMEM Policy.
  It allocates and zero out buffer, and fills in the Intel default settings.

  @param[out] SiPreMemPolicyPpi   The pointer to get Silicon Policy PPI instance

  @retval EFI_SUCCESS             The policy default is initialized.
  @retval EFI_OUT_OF_RESOURCES    Insufficient resources to create buffer
**/
EFI_STATUS
EFIAPI
SiCreatePreMemConfigBlocks (
  OUT  SI_PREMEM_POLICY_PPI **SiPreMemPolicyPpi
  )
{
  UINT16               TotalBlockSize;
  EFI_STATUS           Status;
  SI_PREMEM_POLICY_PPI *SiPreMemPolicy;
  UINT16               RequiredSize;

  SiPreMemPolicy = NULL;
  //
  // TotalBlockSize = Pch , SA, ME and CPU config block size.
  //
  TotalBlockSize = PchGetPreMemConfigBlockTotalSize () +
                   MeGetConfigBlockTotalSizePreMem () +
                   SaGetConfigBlockTotalSizePreMem () +
                   CpuGetPreMemConfigBlockTotalSize ();
  DEBUG ((DEBUG_INFO, "TotalBlockSize = 0x%x\n", TotalBlockSize));

  RequiredSize = sizeof (CONFIG_BLOCK_TABLE_HEADER) + TotalBlockSize;

  Status = CreateConfigBlockTable (RequiredSize, (VOID *)&SiPreMemPolicy);
  ASSERT_EFI_ERROR (Status);

  //
  // General initialization
  //
  SiPreMemPolicy->TableHeader.Header.Revision = SI_PREMEM_POLICY_REVISION;
  //
  // Add config blocks.
  //
  Status = PchAddPreMemConfigBlocks ((VOID *) SiPreMemPolicy);
  ASSERT_EFI_ERROR (Status);
  Status = MeAddConfigBlocksPreMem ((VOID *) SiPreMemPolicy);
  ASSERT_EFI_ERROR (Status);
  Status = SaAddConfigBlocksPreMem ((VOID *) SiPreMemPolicy);
  ASSERT_EFI_ERROR (Status);
  Status = CpuAddPreMemConfigBlocks ((VOID *) SiPreMemPolicy);
  ASSERT_EFI_ERROR (Status);
  //
  // Assignment for returning SaInitPolicy config block base address
  //
  *SiPreMemPolicyPpi = SiPreMemPolicy;
  return Status;
}

/**
  SiPreMemInstallPolicyPpi installs SiPreMemPolicyPpi.
  While installed, RC assumes the Policy is ready and finalized. So please update and override
  any setting before calling this function.

  @param[in] SiPreMemPolicyPpi   The pointer to Silicon Policy PPI instance

  @retval EFI_SUCCESS            The policy is installed.
  @retval EFI_OUT_OF_RESOURCES   Insufficient resources to create buffer
**/
EFI_STATUS
EFIAPI
SiPreMemInstallPolicyPpi (
  IN  SI_PREMEM_POLICY_PPI *SiPolicyPreMemPpi
  )
{
  EFI_STATUS             Status;
  EFI_PEI_PPI_DESCRIPTOR *SiPolicyPreMemPpiDesc;

  SiPolicyPreMemPpiDesc = (EFI_PEI_PPI_DESCRIPTOR *) AllocateZeroPool (sizeof (EFI_PEI_PPI_DESCRIPTOR));
  if (SiPolicyPreMemPpiDesc == NULL) {
    ASSERT (FALSE);
    return EFI_OUT_OF_RESOURCES;
  }

  SiPolicyPreMemPpiDesc->Flags = EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST;
  SiPolicyPreMemPpiDesc->Guid  = &gSiPreMemPolicyPpiGuid;
  SiPolicyPreMemPpiDesc->Ppi   = SiPolicyPreMemPpi;

  //
  // Print whole PCH_POLICY_PPI and serial out.
  //
  PchPreMemPrintPolicyPpi (SiPolicyPreMemPpi);
  //
  // Print ME config blocks and serial out.
  //
  MePrintPolicyPpiPreMem (SiPolicyPreMemPpi);
  //
  // Print whole SI_POLICY_PPI and serial out.
  //
  SaPrintPolicyPpiPreMem (SiPolicyPreMemPpi);
  //
  // Print whole CPU of SI_PREMEM_POLICY_PPI and serial out.
  //
  CpuPreMemPrintPolicy (SiPolicyPreMemPpi);

  //
  // Install Silicon Policy PPI
  //
  Status = PeiServicesInstallPpi (SiPolicyPreMemPpiDesc);
  ASSERT_EFI_ERROR (Status);
  return Status;
}
