/** @file
  This file is PeiCpuPolicy library.

Copyright (c) 2017, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include "PeiCpuPolicyLibrary.h"
#include <SaAccess.h>
#include <IndustryStandard/Pci22.h>
#include <Library/MmPciLib.h>
#include <Library/SaPlatformLib.h>
#include <Library/ConfigBlockLib.h>
#include <Library/PostCodeLib.h>

#ifndef FSP_FLAG
/**
  Get the next microcode patch pointer.

  @param[in, out] MicrocodeData - Input is a pointer to the last microcode patch address found,
                                  and output points to the next patch address found.

  @retval EFI_SUCCESS           - Patch found.
  @retval EFI_NOT_FOUND         - Patch not found.
**/
EFI_STATUS
EFIAPI
RetrieveMicrocode (
  IN OUT CPU_MICROCODE_HEADER **MicrocodeData
  )
{
  UINTN                MicrocodeStart;
  UINTN                MicrocodeEnd;
  UINTN                TotalSize;

  if ((FixedPcdGet32 (PcdFlashMicrocodeFvBase) == 0) || (FixedPcdGet32 (PcdFlashMicrocodeFvSize) == 0)) {
    return EFI_NOT_FOUND;
  }

  ///
  /// Microcode binary in SEC
  ///
  MicrocodeStart = (UINTN) FixedPcdGet32 (PcdFlashMicrocodeFvBase) +
          ((EFI_FIRMWARE_VOLUME_HEADER *) (UINTN) FixedPcdGet32 (PcdFlashMicrocodeFvBase))->HeaderLength +
          sizeof (EFI_FFS_FILE_HEADER);

  MicrocodeEnd = (UINTN) FixedPcdGet32 (PcdFlashMicrocodeFvBase) + (UINTN) FixedPcdGet32 (PcdFlashMicrocodeFvSize);

  if (*MicrocodeData == NULL) {
    *MicrocodeData = (CPU_MICROCODE_HEADER *) (UINTN) MicrocodeStart;
  } else {
    if (*MicrocodeData < (CPU_MICROCODE_HEADER *) (UINTN) MicrocodeStart) {
      DEBUG ((DEBUG_INFO, "[CpuPolicy]*MicrocodeData < MicrocodeStart \n"));
      return EFI_NOT_FOUND;
    }

    TotalSize = (UINTN) ((*MicrocodeData)->TotalSize);
    if (TotalSize == 0) {
      TotalSize = 2048;
    }

    *MicrocodeData = (CPU_MICROCODE_HEADER *) ((UINTN)*MicrocodeData + TotalSize);
    if (*MicrocodeData >= (CPU_MICROCODE_HEADER *) (UINTN) (MicrocodeEnd) || (*MicrocodeData)->TotalSize == (UINT32) -1) {
      DEBUG ((DEBUG_INFO, "[CpuPolicy]*MicrocodeData >= MicrocodeEnd \n"));
      return EFI_NOT_FOUND;
    }
  }
  return EFI_SUCCESS;
}

/**
  Get the microcode patch pointer.

  @retval EFI_PHYSICAL_ADDRESS - Address of the microcode patch, or NULL if not found.
**/
EFI_PHYSICAL_ADDRESS
PlatformCpuLocateMicrocodePatch (
  VOID
  )
{
  EFI_STATUS           Status;
  CPU_MICROCODE_HEADER *MicrocodeData;
  EFI_CPUID_REGISTER   Cpuid;
  UINT32               UcodeRevision;
  UINTN                MicrocodeBufferSize;
  VOID                 *MicrocodeBuffer = NULL;

  AsmCpuid (
    CPUID_VERSION_INFO,
    &Cpuid.RegEax,
    &Cpuid.RegEbx,
    &Cpuid.RegEcx,
    &Cpuid.RegEdx
    );

  UcodeRevision = GetCpuUcodeRevision ();
  MicrocodeData = NULL;
  while (TRUE) {
    ///
    /// Find the next patch address
    ///
    Status = RetrieveMicrocode (&MicrocodeData);
    DEBUG ((DEBUG_INFO, "MicrocodeData = %x\n", MicrocodeData));

    if (Status != EFI_SUCCESS) {
      break;
    } else if (CheckMicrocode (Cpuid.RegEax, MicrocodeData, &UcodeRevision)) {
      break;
    }
  }

  if (EFI_ERROR (Status)) {
    return (EFI_PHYSICAL_ADDRESS) (UINTN) NULL;
  }

  ///
  /// Check that microcode patch size is <= 128K max size,
  /// then copy the patch from FV to temp buffer for faster access.
  ///
  MicrocodeBufferSize = (UINTN) MicrocodeData->TotalSize;

  if (MicrocodeBufferSize <= MAX_MICROCODE_PATCH_SIZE) {
    MicrocodeBuffer = AllocatePages (EFI_SIZE_TO_PAGES (MicrocodeBufferSize));
    if (MicrocodeBuffer != NULL) {
      DEBUG(( DEBUG_INFO, "Copying Microcode to temp buffer.\n"));
      CopyMem (MicrocodeBuffer, MicrocodeData, MicrocodeBufferSize);

      return (EFI_PHYSICAL_ADDRESS) (UINTN) MicrocodeBuffer;
    } else {
      DEBUG(( DEBUG_ERROR, "Failed to allocate enough memory for Microcode Patch.\n"));
    }
  } else {
    DEBUG(( DEBUG_ERROR, "Microcode patch size is greater than max allowed size of 128K.\n"));
  }
  return (EFI_PHYSICAL_ADDRESS) (UINTN) NULL;
}
#endif

/**
  Load Config block default

  @param[in] ConfigBlockPointer         Pointer to config block
**/
VOID
LoadCpuConfigDefault (
  IN VOID          *ConfigBlockPointer
  )
{
  CPU_CONFIG  *CpuConfig;
  CpuConfig  = ConfigBlockPointer;

  DEBUG ((DEBUG_INFO, "CpuConfig->Header.GuidHob.Name = %g\n", &CpuConfig->Header.GuidHob.Name));
  DEBUG ((DEBUG_INFO, "CpuConfig->Header.GuidHob.Header.HobLength = 0x%x\n", CpuConfig->Header.GuidHob.Header.HobLength));

  /********************************
    CPU configuration
  ********************************/
  CpuConfig->AesEnable             = CPU_FEATURE_ENABLE;
  CpuConfig->EnableRsr             = CPU_FEATURE_ENABLE;
  CpuConfig->SmmbaseSwSmiNumber    = (UINTN) PcdGet8 (PcdSmmbaseSwSmi);
#ifndef FSP_FLAG
  CpuConfig->MicrocodePatchAddress = PlatformCpuLocateMicrocodePatch ();
#endif
}


/**
  Load Config block default

  @param[in] ConfigBlockPointer         Pointer to config block
**/
VOID
LoadCpuPowerMgmtBasicConfigDefault (
  IN VOID          *ConfigBlockPointer
  )
{
  CPU_POWER_MGMT_BASIC_CONFIG  *CpuPowerMgmtBasicConfig;
  CPU_SKU      CpuSku;
  MSR_REGISTER TempMsr;

  CpuPowerMgmtBasicConfig = ConfigBlockPointer;
  CpuSku                  = GetCpuSku();

  DEBUG ((DEBUG_INFO, "CpuPowerMgmtBasicConfig->Header.GuidHob.Name = %g\n", &CpuPowerMgmtBasicConfig->Header.GuidHob.Name));
  DEBUG ((DEBUG_INFO, "CpuPowerMgmtBasicConfig->Header.GuidHob.Header.HobLength = 0x%x\n", CpuPowerMgmtBasicConfig->Header.GuidHob.Header.HobLength));

  /********************************
    CPU Power Management Basic configuration
  ********************************/
  CpuPowerMgmtBasicConfig->Hwp                          = TRUE;
  CpuPowerMgmtBasicConfig->HdcControl                   = TRUE;
  CpuPowerMgmtBasicConfig->PowerLimit2                  = TRUE;
  CpuPowerMgmtBasicConfig->PowerLimit3Lock              = TRUE;
  ///
  /// Initialize RATL (Runtime Average Temperature Limit) Config for SKL Y series.
  ///
  if (CpuSku == EnumCpuUlx) {
    CpuPowerMgmtBasicConfig->TccActivationOffset        = 10;
    CpuPowerMgmtBasicConfig->TccOffsetTimeWindowForRatl = 5000; // 5 sec
    CpuPowerMgmtBasicConfig->TccOffsetClamp             = CPU_FEATURE_ENABLE;
  }
  CpuPowerMgmtBasicConfig->TurboMode                    = TRUE;

  TempMsr.Qword = AsmReadMsr64 (MSR_TURBO_RATIO_LIMIT);
  CpuPowerMgmtBasicConfig->OneCoreRatioLimit = TempMsr.Bytes.FirstByte;
  CpuPowerMgmtBasicConfig->TwoCoreRatioLimit = TempMsr.Bytes.SecondByte;
  CpuPowerMgmtBasicConfig->ThreeCoreRatioLimit = TempMsr.Bytes.ThirdByte;
  CpuPowerMgmtBasicConfig->FourCoreRatioLimit = TempMsr.Bytes.FouthByte;
  CpuPowerMgmtBasicConfig->FiveCoreRatioLimit = TempMsr.Bytes.FifthByte;
  CpuPowerMgmtBasicConfig->SixCoreRatioLimit = TempMsr.Bytes.SixthByte;
  CpuPowerMgmtBasicConfig->SevenCoreRatioLimit = TempMsr.Bytes.SeventhByte;
  CpuPowerMgmtBasicConfig->EightCoreRatioLimit = TempMsr.Bytes.EighthByte;
}

/**
  Load Config block default

  @param[in] ConfigBlockPointer         Pointer to config block
**/
VOID
LoadCpuPowerMgmtCustomConfigDefault (
  IN VOID          *ConfigBlockPointer
  )
{
  CPU_POWER_MGMT_CUSTOM_CONFIG  *CpuPowerMgmtCustomConfig;
  CpuPowerMgmtCustomConfig = ConfigBlockPointer;

  DEBUG ((DEBUG_INFO, "CpuPowerMgmtCustomConfig->Header.GuidHob.Name = %g\n", &CpuPowerMgmtCustomConfig->Header.GuidHob.Name));
  DEBUG ((DEBUG_INFO, "CpuPowerMgmtCustomConfig->Header.GuidHob.Header.HobLength = 0x%x\n", CpuPowerMgmtCustomConfig->Header.GuidHob.Header.HobLength));

  /********************************
    CPU Power Management Custom configuration
  ********************************/
  CpuPowerMgmtCustomConfig->CustomRatioTable.Cpuid = (UINT16) ((GetCpuFamily() | GetCpuStepping()) & (0x0FFF));
}

/**
  Load Config block default

  @param[in] ConfigBlockPointer         Pointer to config block
**/
VOID
LoadCpuTestConfigDefault (
  IN VOID          *ConfigBlockPointer
  )
{
  CPU_TEST_CONFIG  *CpuTestConfig;
  CPU_SKU          CpuSku;
  CpuTestConfig = ConfigBlockPointer;

  DEBUG ((DEBUG_INFO, "CpuTestConfig->Header.GuidHob.Name = %g\n", &CpuTestConfig->Header.GuidHob.Name));
  DEBUG ((DEBUG_INFO, "CpuTestConfig->Header.GuidHob.Header.HobLength = 0x%x\n", CpuTestConfig->Header.GuidHob.Header.HobLength));

  CpuSku = GetCpuSku();
  /********************************
    CPU Test configuration
  ********************************/
  CpuTestConfig->MlcStreamerPrefetcher    = CPU_FEATURE_ENABLE;
  CpuTestConfig->MlcSpatialPrefetcher     = CPU_FEATURE_ENABLE;
  CpuTestConfig->MonitorMwaitEnable       = CPU_FEATURE_ENABLE;
  CpuTestConfig->MachineCheckEnable       = CPU_FEATURE_ENABLE;
  CpuTestConfig->DebugInterfaceLockEnable = CPU_FEATURE_ENABLE;
  CpuTestConfig->ApIdleManner             = 2; // AP Idle Manner default as Mwait Loop
  CpuTestConfig->ApHandoffManner          = 2; // AP Handoff to OS default as Mwait Loop
  ///
  /// Processor Trace
  ///
  CpuTestConfig->ProcTraceMemSize         = EnumProcTraceMemDisable;

  /**
    This policy should be used to enable or disable Voltage Optimization feature.
    Recommended defaults:
     Enable  - For Mobile SKUs(U/Y)
     Disable - Rest of all SKUs other than Mobile.
  **/
  if ((CpuSku == EnumCpuUlx) || (CpuSku == EnumCpuUlt)){
    CpuTestConfig->VoltageOptimization = CPU_FEATURE_ENABLE;
  } else {
    CpuTestConfig->VoltageOptimization = CPU_FEATURE_DISABLE;
  }
}

/**
  Load Config block default

  @param[in] ConfigBlockPointer         Pointer to config block
**/
VOID
LoadCpuPidTestConfigDefault (
  IN VOID          *ConfigBlockPointer
  )
{
  CPU_PID_TEST_CONFIG  *CpuPidTestConfig;
  CpuPidTestConfig = ConfigBlockPointer;

  DEBUG ((DEBUG_INFO, "CpuPidTestConfig->Header.GuidHob.Name = %g\n", &CpuPidTestConfig->Header.GuidHob.Name));
  DEBUG ((DEBUG_INFO, "CpuPidTestConfig->Header.GuidHob.Header.HobLength = 0x%x\n", CpuPidTestConfig->Header.GuidHob.Header.HobLength));

  /********************************
    CPU PID Test configuration
  ********************************/
}

/**
  Load Config block default

  @param[in] ConfigBlockPointer         Pointer to config block
**/
VOID
LoadCpuPowerMgmtTestConfigDefault (
  IN VOID          *ConfigBlockPointer
  )
{
  CPU_POWER_MGMT_TEST_CONFIG  *CpuPowerMgmtTestConfig;
  CPU_FAMILY                  CpuFamily;
  CpuPowerMgmtTestConfig = ConfigBlockPointer;

  DEBUG ((DEBUG_INFO, "CpuPowerMgmtTestConfig->Header.GuidHob.Name = %g\n", &CpuPowerMgmtTestConfig->Header.GuidHob.Name));
  DEBUG ((DEBUG_INFO, "CpuPowerMgmtTestConfig->Header.GuidHob.Header.HobLength = 0x%x\n", CpuPowerMgmtTestConfig->Header.GuidHob.Header.HobLength));

  /********************************
    CPU Power Management Test configuration
  ********************************/
  CpuPowerMgmtTestConfig->Eist                          = TRUE;
  CpuPowerMgmtTestConfig->EnergyEfficientPState         = TRUE;
  CpuPowerMgmtTestConfig->EnergyEfficientTurbo          = TRUE;
  CpuPowerMgmtTestConfig->BiProcHot                     = TRUE;
  CpuPowerMgmtTestConfig->DisableProcHotOut             = TRUE;
  CpuPowerMgmtTestConfig->AutoThermalReporting          = TRUE;
  CpuPowerMgmtTestConfig->ThermalMonitor                = TRUE;
  CpuPowerMgmtTestConfig->Cx                            = TRUE;
  CpuPowerMgmtTestConfig->PmgCstCfgCtrlLock             = TRUE;
  CpuPowerMgmtTestConfig->C1e                           = TRUE;
  CpuPowerMgmtTestConfig->C1AutoDemotion                = TRUE;
  CpuPowerMgmtTestConfig->C1UnDemotion                  = TRUE;
  CpuPowerMgmtTestConfig->C3AutoDemotion                = TRUE;
  CpuPowerMgmtTestConfig->C3UnDemotion                  = TRUE;
  CpuPowerMgmtTestConfig->PkgCStateDemotion             = TRUE;
  CpuPowerMgmtTestConfig->PkgCStateUnDemotion           = TRUE;
  CpuPowerMgmtTestConfig->CStatePreWake                 = TRUE;
  CpuPowerMgmtTestConfig->RaceToHalt                    = TRUE;
  CpuPowerMgmtTestConfig->CstateLatencyControl0Irtl     = C3_LATENCY;
  CpuPowerMgmtTestConfig->CstateLatencyControl1Irtl     = C6_C7_SHORT_LATENCY;
  CpuPowerMgmtTestConfig->CstateLatencyControl2Irtl     = C6_C7_LONG_LATENCY;
  CpuPowerMgmtTestConfig->CstateLatencyControl3Irtl     = C8_LATENCY;
  CpuPowerMgmtTestConfig->CstateLatencyControl4Irtl     = C9_LATENCY;
  //
  // If PS4 is disabled, program 2750us to MSR_C_STATE_LATENCY_CONTROL_5
  //
  CpuPowerMgmtTestConfig->CstateLatencyControl5Irtl     = C10_LATENCY;
  CpuPowerMgmtTestConfig->PkgCStateLimit                = PkgAuto;
  CpuPowerMgmtTestConfig->CstateLatencyControl0TimeUnit = TimeUnit1024ns;
  CpuPowerMgmtTestConfig->CstateLatencyControl1TimeUnit = TimeUnit1024ns;
  CpuPowerMgmtTestConfig->CstateLatencyControl2TimeUnit = TimeUnit1024ns;
  CpuPowerMgmtTestConfig->CstateLatencyControl3TimeUnit = TimeUnit1024ns;
  CpuPowerMgmtTestConfig->CstateLatencyControl4TimeUnit = TimeUnit1024ns;
  CpuPowerMgmtTestConfig->CstateLatencyControl5TimeUnit = TimeUnit1024ns;
  CpuPowerMgmtTestConfig->CustomPowerUnit               = PowerUnit125MilliWatts;
  CpuPowerMgmtTestConfig->PpmIrmSetting                 = PpmIrmPairFixedPriority;

  //
  // Pkg C-state Demotion/Un Demotion
  //
  CpuFamily = GetCpuFamily();
  if((CpuFamily == EnumCpuSklUltUlx) || (CpuFamily == EnumCpuSklDtHalo)){
    CpuPowerMgmtTestConfig->PkgCStateDemotion                  = TRUE;
  } else if((CpuFamily == EnumCpuKblUltUlx) || (CpuFamily == EnumCpuKblDtHalo)) {
    CpuPowerMgmtTestConfig->PkgCStateDemotion                  = FALSE;
  }
  if((CpuFamily == EnumCpuSklUltUlx) || (CpuFamily == EnumCpuSklDtHalo)){
    CpuPowerMgmtTestConfig->PkgCStateDemotion                  = TRUE;
  } else if((CpuFamily == EnumCpuKblUltUlx) || (CpuFamily == EnumCpuKblDtHalo)) {
    CpuPowerMgmtTestConfig->PkgCStateDemotion                  = FALSE;
  }
}

static COMPONENT_BLOCK_ENTRY  mCpuIpBlocks [] = {
  {&gCpuConfigGuid,                     sizeof (CPU_CONFIG),                         CPU_CONFIG_REVISION,                        LoadCpuConfigDefault},
  {&gCpuPowerMgmtBasicConfigGuid,       sizeof (CPU_POWER_MGMT_BASIC_CONFIG),        CPU_POWER_MGMT_BASIC_CONFIG_REVISION,       LoadCpuPowerMgmtBasicConfigDefault},
  {&gCpuPowerMgmtCustomConfigGuid,      sizeof (CPU_POWER_MGMT_CUSTOM_CONFIG),       CPU_POWER_MGMT_CUSTOM_CONFIG_REVISION,      LoadCpuPowerMgmtCustomConfigDefault},
  {&gCpuTestConfigGuid,                 sizeof (CPU_TEST_CONFIG),                    CPU_TEST_CONFIG_REVISION,                   LoadCpuTestConfigDefault},
  {&gCpuPidTestConfigGuid,              sizeof (CPU_PID_TEST_CONFIG),                CPU_PID_TEST_CONFIG_REVISION,               LoadCpuPidTestConfigDefault},
  {&gCpuPowerMgmtTestConfigGuid,        sizeof (CPU_POWER_MGMT_TEST_CONFIG),         CPU_POWER_MGMT_TEST_CONFIG_REVISION,        LoadCpuPowerMgmtTestConfigDefault},
};

/**
  Get CPU config block table total size.

  @retval Size of CPU config block table
**/
UINT16
EFIAPI
CpuGetConfigBlockTotalSize (
  VOID
  )
{
  return GetComponentConfigBlockTotalSize (&mCpuIpBlocks[0], sizeof (mCpuIpBlocks) / sizeof (COMPONENT_BLOCK_ENTRY));
}

/**
  CpuAddConfigBlocks add all Cpu config blocks.

  @param[in] ConfigBlockTableAddress    The pointer to add CPU config blocks

  @retval EFI_SUCCESS                   The policy default is initialized.
  @retval EFI_OUT_OF_RESOURCES          Insufficient resources to create buffer
**/
EFI_STATUS
EFIAPI
CpuAddConfigBlocks (
  IN     VOID      *ConfigBlockTableAddress
  )
{
  EFI_STATUS Status;
  DEBUG((DEBUG_INFO, "CPU Post-Mem Entry \n"));
  PostCode (0xC00);

  Status = AddComponentConfigBlocks (ConfigBlockTableAddress, &mCpuIpBlocks[0], sizeof (mCpuIpBlocks) / sizeof (COMPONENT_BLOCK_ENTRY));
  DEBUG ((DEBUG_INFO, "CpuAddConfigBlocks Done \n"));
  PostCode (0xC09);

  return Status;
}

