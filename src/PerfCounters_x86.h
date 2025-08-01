/* -*- Mode: C++; tab-width: 8; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
// This file is included from PerfCounters.cc

static bool has_kvm_in_txcp_bug;
static bool has_xen_pmi_bug;
static bool supports_txcp;

/**
 * Return the detected, known microarchitecture of this CPU, or don't
 * return; i.e. never return UnknownCpu.
 *
 * Another way to do this would be to read the pmu type under
 * /sys/devices/.../caps/pmu_name. There are tradeoffs:
 *   * With the current approach, rr works with old kernels that haven't
 * been updated with specific knowledge of the CPU type. Reading
 * `pmu_name`, rr would not work.
 *   * Reading `pmu_name`, rr would work with new CPUs that use an
 * existing PMU type, if the kernel is new enough to know about those
 * CPUs. With the current approach users have to have an rr that has
 * been updated for those CPUs.
 * Assuming that it's easier to update rr than update one's kernel,
 * the current approach seems a little better.
 *
 * This detects the overall microarchitecture, which we also use
 * as the microarchitecture identifier for the P-cores of that architecture
 * in a hybrid setup.
 */
static CpuMicroarch compute_cpu_microarch() {
  auto cpuid_vendor = cpuid(CPUID_GETVENDORSTRING, 0);
  char vendor[12];
  memcpy(&vendor[0], &cpuid_vendor.ebx, 4);
  memcpy(&vendor[4], &cpuid_vendor.edx, 4);
  memcpy(&vendor[8], &cpuid_vendor.ecx, 4);
  if (strncmp(vendor, "GenuineIntel", sizeof(vendor)) &&
      strncmp(vendor, "AuthenticAMD", sizeof(vendor))) {
    CLEAN_FATAL() << "Unknown CPU vendor '" << vendor << "'";
  }

  auto cpuid_data = cpuid(CPUID_GETFEATURES, 0);
  unsigned int cpu_type = cpuid_data.eax & 0xF0FF0;
  unsigned int ext_family = (cpuid_data.eax >> 20) & 0xff;
  switch (cpu_type) {
    case 0x006F0:
    case 0x10660:
      return IntelMerom;
    case 0x10670:
    case 0x106D0:
      return IntelPenryn;
    case 0x106A0:
    case 0x106E0:
    case 0x206E0:
      return IntelNehalem;
    case 0x20650:
    case 0x206C0:
    case 0x206F0:
      return IntelWestmere;
    case 0x206A0:
    case 0x206D0:
    case 0x306e0:
      return IntelSandyBridge;
    case 0x306A0:
      return IntelIvyBridge;
    case 0x306C0: /* Devil's Canyon */
    case 0x306F0:
    case 0x40650:
    case 0x40660:
      return IntelHaswell;
    case 0x306D0:
    case 0x40670:
    case 0x406F0:
    case 0x50660:
      return IntelBroadwell;
    case 0x406e0:
    case 0x50650:
    case 0x506e0:
      return IntelSkylake;
    case 0x30670:
    case 0x406c0:
    case 0x50670:
      return IntelSilvermont;
    case 0x506f0:
    case 0x706a0:
    case 0x506c0:
      return IntelGoldmont;
    case 0x906c0:
      return IntelTremont;
    case 0x706e0:
    case 0x606a0:
    case 0x80660:
      return IntelIceLake;
    case 0x806c0:
    case 0x806d0:
      return IntelTigerLake;
    case 0x806e0:
    case 0x906e0:
      return IntelKabyLake;
    case 0xa0650:
    case 0xa0660:
      return IntelCometLake;
    case 0xa0670:
      return IntelRocketLake;
    case 0x90670:
    case 0x906a0:
      return IntelAlderLake;
    case 0xb0670:
    case 0xb06a0:
    case 0xb06f0:
      return IntelRaptorLake;
    case 0x806f0:
      return IntelSapphireRapids;
    case 0xc06f0:
      return IntelEmeraldRapids;
    case 0xa06d0:
      return IntelGraniteRapids;
    case 0xa06a0:
      return IntelMeteorLake;
    case 0xb06d0:
      return IntelLunarLake;
    case 0xc0660:
      return IntelArrowLake;
    case 0xf20:  // Piledriver
    case 0x30f00:  // Steamroller
      return AMDF15;
    case 0x00f10: // A8-3530MX, Naples, Whitehaven, Summit Ridge, Snowy Owl (Zen), Milan (Zen 3) (UNTESTED)
      if (ext_family == 8) {
        return AMDZen;
      } else if (ext_family == 0xa) {
        return AMDZen3;
      } else if (ext_family == 3) {
        return AMDF15;
      }
      break;
    case 0x00f80: // Colfax, Pinnacle Ridge (Zen+), Chagall (Zen3) (UNTESTED)
      if (ext_family == 8) {
        return AMDZen;
      } else if (ext_family == 0xa) {
        return AMDZen3;
      }
      break;
    case 0x10f10: // Raven Ridge, Great Horned Owl (Zen) (UNTESTED)
    case 0x10f80: // Banded Kestrel (Zen), Picasso (Zen+), 7975WX (Zen2)
    case 0x20f00: // Dali (Zen)
      if (ext_family == 8) {
        return AMDZen;
      } else if (ext_family == 0xa) {
        return AMDZen2;
      }
      break;
    case 0x30f10: // Rome, Castle Peak (Zen 2)
    case 0x60f00: // Renoir (Zen 2)
    case 0x70f10: // Matisse (Zen 2)
    case 0x60f80: // Lucienne (Zen 2)
    case 0x90f00: // Van Gogh (Zen 2)
      if (ext_family == 8) {
        return AMDZen2;
      }
      break;
    case 0x20f10: // Vermeer (Zen 3)
    case 0x50f00: // Cezanne (Zen 3)
    case 0x40f40: // Rembrandt (Zen 3+)
      return AMDZen3;
    case 0x60f10: // Raphael (Zen 4)
    case 0x70f40: // Phoenix (Zen 4)
    case 0x70f50: // Hawk Point (Zen 4)
      return AMDZen4;
    case 0x20f40: // Strix Point (Zen 5)
      return AMDZen5;
    default:
      break;
  }

  if (!strncmp(vendor, "AuthenticAMD", sizeof(vendor))) {
    CLEAN_FATAL() << "AMD CPU type " << HEX(cpu_type) <<
                     " (ext family " << HEX(ext_family) << ") unknown";
  } else {
    CLEAN_FATAL() << "Intel CPU type " << HEX(cpu_type) << " unknown";
  }
  return UnknownCpu; // not reached
}

static vector<CPUInfo> compute_cpus_info() {
  vector<CPUInfo> result;
  auto groups = CPUs::get().cpu_groups();
  if (groups.empty()) {
    // Only one kind of CPU core.
    result.push_back({compute_cpu_microarch(), PERF_TYPE_RAW});
    return result;
  }

  for (int cpu : CPUs::get().initial_affinity()) {
    if (cpu < static_cast<int>(result.size()) &&
        result[cpu].microarch != UnknownCpu) {
      // This cpu belongs to a group we already computed the microarch for.
      // Using groups like this lets us avoid having to schedule this thread
      // on every single CPU in the system.
      continue;
    }
    while (static_cast<int>(result.size()) < cpu) {
      result.push_back({UnknownCpu, PERF_TYPE_RAW});
    }
    if (!CPUs::set_affinity_to_cpu(cpu)) {
      FATAL() << "Can't set affinity to previously allowed CPU";
    }
    CpuMicroarch uarch = compute_cpu_microarch();
    // May be overwritten below if this is part of a known hybrid core grouping.
    result.push_back({uarch, PERF_TYPE_RAW});
    // Fill in `result` for all CPUs in the same group as the current CPU.
    // This avoids having to schedule this thread on every single CPU in the
    // system.
    for (auto group : groups) {
      if (group.start_cpu <= cpu && cpu < group.end_cpu) {
        result.resize(group.end_cpu);
        if (group.name == "atom") {
          switch (uarch) {
          case IntelAlderLake:
          case IntelRaptorLake:
            uarch = IntelGracemont;
            break;
          case IntelMeteorLake:
            uarch = IntelCrestmont;
            break;
          case IntelLunarLake:
          case IntelArrowLake:
            // Some Arrow Lakes use Crestmont E-cores maybe? Hopefully doesn't matter
            // for the PMU.
            uarch = IntelSkymont;
            break;
          default:
            FATAL() << "Atom architecture detected but not known for " << uarch;
          }
        } else if (group.name == "lowpower") {
          switch (uarch) {
          case IntelArrowLake:
            uarch = IntelCrestmont;
            break;
          default:
            FATAL() << "Lowpower architecture detected but not known for " << uarch;
          }
        } else if (group.name != "core") {
          FATAL() << "Hybrid architecture group name not known: " << group.name;
        }
        for (int i = group.start_cpu; i < group.end_cpu; ++i) {
          result[i] = {uarch, group.type};
        }
        break;
      }
    }
  }
  CPUs::get().restore_initial_affinity();
  return result;
}

static void check_for_kvm_in_txcp_bug(const perf_event_attrs &perf_attr) {
  int64_t count = 0;
  struct perf_event_attr attr = perf_attr.ticks;
  attr.config |= IN_TXCP;
  attr.sample_period = 0;
  bool disabled_txcp;
  ScopedFd fd = start_counter(0, -1, &attr, &disabled_txcp);
  if (fd.is_open() && !disabled_txcp) {
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    do_branches();
    count = read_counter(fd);
  }

  supports_txcp = count > 0;
  has_kvm_in_txcp_bug = supports_txcp && count < NUM_BRANCHES;
  LOG(debug) << "supports txcp=" << supports_txcp;
  LOG(debug) << "has_kvm_in_txcp_bug=" << has_kvm_in_txcp_bug
             << " count=" << count;
}

static void check_for_xen_pmi_bug(const perf_event_attrs &perf_attr) {
  int32_t count = -1;
  struct perf_event_attr attr = perf_attr.ticks;
  attr.sample_period = NUM_BRANCHES - 1;
  ScopedFd fd = start_counter(0, -1, &attr);
  if (fd.is_open()) {
    // Do NUM_BRANCHES conditional branches that can't be optimized out.
    // 'accumulator' is always odd and can't be zero
    uint32_t accumulator = uint32_t(rand()) * 2 + 1;
    int raw_fd = fd;
    asm volatile(
#if defined(__x86_64__)
        "mov %[_SYS_ioctl], %%rax;"
        "mov %[raw_fd], %%edi;"
        "xor %%rdx, %%rdx;"
        "mov %[_PERF_EVENT_IOC_ENABLE], %%rsi;"
        "syscall;"
        "cmp $-4095, %%rax;"
        "jae 2f;"
        "mov %[_SYS_ioctl], %%rax;"
        "mov %[_PERF_EVENT_IOC_RESET], %%rsi;"
        "syscall;"
        // From this point on all conditional branches count!
        "cmp $-4095, %%rax;"
        "jae 2f;"
        // Reset the counter period to the desired value.
        "mov %[_SYS_ioctl], %%rax;"
        "mov %[_PERF_EVENT_IOC_PERIOD], %%rsi;"
        "mov %[period], %%rdx;"
        "syscall;"
        "cmp $-4095, %%rax;"
        "jae 2f;"
        "mov %[_iterations], %%rax;"
        "1: dec %%rax;"
        // Multiply by 7.
        "mov %[accumulator], %%edx;"
        "shl $3, %[accumulator];"
        "sub %%edx, %[accumulator];"
        // Add 2.
        "add $2, %[accumulator];"
        // Mask off bits.
        "and $0xffffff, %[accumulator];"
        // And loop.
        "test %%rax, %%rax;"
        "jnz 1b;"
        "mov %[_PERF_EVENT_IOC_DISABLE], %%rsi;"
        "mov %[_SYS_ioctl], %%rax;"
        "xor %%rdx, %%rdx;"
        // We didn't touch rdi.
        "syscall;"
        "cmp $-4095, %%rax;"
        "jae 2f;"
        "movl $0, %[count];"
        "2: nop;"
#elif defined(__i386__)
        "mov %[_SYS_ioctl], %%eax;"
        "mov %[raw_fd], %%ebx;"
        "xor %%edx, %%edx;"
        "mov %[_PERF_EVENT_IOC_ENABLE], %%ecx;"
        "int $0x80;"
        "cmp $-4095, %%eax;"
        "jae 2f;"
        "mov %[_SYS_ioctl], %%eax;"
        "mov %[_PERF_EVENT_IOC_RESET], %%ecx;"
        "int $0x80;"
        // From this point on all conditional branches count!
        "cmp $-4095, %%eax;"
        "jae 2f;"
        // Reset the counter period to the desired value.
        "mov %[_SYS_ioctl], %%eax;"
        "mov %[_PERF_EVENT_IOC_PERIOD], %%ecx;"
        "mov %[period], %%edx;"
        "int $0x80;"
        "cmp $-4095, %%eax;"
        "jae 2f;"
        "mov %[_iterations], %%eax;"
        "1: dec %%eax;"
        // Multiply by 7.
        "mov %[accumulator], %%edx;"
        "shll $3, %[accumulator];"
        "sub %%edx, %[accumulator];"
        // Add 2.
        "addl $2, %[accumulator];"
        // Mask off bits.
        "andl $0xffffff, %[accumulator];"
        // And loop.
        "test %%eax, %%eax;"
        "jnz 1b;"
        "mov %[_PERF_EVENT_IOC_DISABLE], %%ecx;"
        "mov %[_SYS_ioctl], %%eax;"
        "xor %%edx, %%edx;"
        // We didn't touch rdi.
        "int $0x80;"
        "cmp $-4095, %%eax;"
        "jae 2f;"
        "movl $0, %[count];"
        "2: nop;"
#else
#error unknown CPU architecture
#endif
        : [accumulator] "+rm"(accumulator), [count] "=rm"(count)
        : [_SYS_ioctl] "i"(SYS_ioctl),
          [_PERF_EVENT_IOC_DISABLE] "i"(PERF_EVENT_IOC_DISABLE),
          [_PERF_EVENT_IOC_ENABLE] "i"(PERF_EVENT_IOC_ENABLE),
          [_PERF_EVENT_IOC_PERIOD] "i"(PERF_EVENT_IOC_PERIOD),
          [_PERF_EVENT_IOC_RESET] "i"(PERF_EVENT_IOC_RESET),
          // The check for the failure of some of our ioctls is in
          // the measured region, so account for that when looping.
          [_iterations] "i"(NUM_BRANCHES - 2),
          [period] "rm"(&attr.sample_period), [raw_fd] "rm"(raw_fd)
        :
#if defined(__x86_64__)
        "rax", "rdx", "rdi", "rsi"
        // `syscall` clobbers rcx and r11.
        ,
        "rcx", "r11"
#elif defined(__i386__)
        "eax", "ebx", "ecx", "edx"
#else
#error unknown CPU architecture
#endif
        );
    // If things worked above, `count` should have been set to 0.
    if (count == 0) {
      count = read_counter(fd);
    }
    // Use 'accumulator' so it can't be optimized out.
    accumulator_sink = accumulator;
  }

  has_xen_pmi_bug = count > NUM_BRANCHES || count == -1;
  if (has_xen_pmi_bug) {
    LOG(debug) << "has_xen_pmi_bug=" << has_xen_pmi_bug << " count=" << count;
    if (!Flags::get().force_things) {
      FATAL()
          << "Overcount triggered by PMU interrupts detected due to Xen PMU "
             "virtualization bug.\n"
             "Aborting. Retry with -F to override, but it will probably\n"
             "fail.";
    } else {
      cpu_improperly_configured = true;
    }
  }
}

static void check_for_zen_speclockmap() {
  // When the SpecLockMap optimization is not disabled, rr will not work
  // reliably (e.g. it would work fine on a single process with a single
  // thread, but not more). When the optimization is disabled, the
  // perf counter for retired lock instructions of type SpecLockMapCommit
  // (on PMC 0x25) stays at 0.
  // See more details at https://github.com/rr-debugger/rr/issues/2034.
  struct perf_event_attr attr;
  // 0x25 == RETIRED_LOCK_INSTRUCTIONS - Counts the number of retired locked instructions
  // + 0x08 == SPECLOCKMAPCOMMIT
  init_perf_event_attr(&attr, PERF_TYPE_RAW, 0x510825);

  ScopedFd fd = start_counter(0, -1, &attr);
  if (fd.is_open()) {
    int atomic = 0;
    int64_t count = read_counter(fd);
    // A lock add is known to increase the perf counter we're looking at.
    asm volatile("lock addl $1, %0": "+m" (atomic));
    if (read_counter(fd) == count) {
      LOG(debug) << "SpecLockMap is disabled";
    } else {
      LOG(debug) << "SpecLockMap is not disabled";
      if (!Flags::get().force_things) {
        CLEAN_FATAL() <<
                "On Zen CPUs, rr will not work reliably unless you disable the "
                "hardware SpecLockMap optimization.\nFor instructions on how to "
                "do this, see https://github.com/rr-debugger/rr/wiki/Zen\n";
      } else {
        cpu_improperly_configured = true;
      }
    }
  }
}

static void check_for_freeze_on_smi() {
  ScopedFd fd = ScopedFd("/sys/devices/cpu/freeze_on_smi", O_RDONLY);
  if (!fd.is_open()) {
    LOG(debug) << "/sys/devices/cpu/freeze_on_smi not present";
    return;
  }

  char freeze_on_smi = 0;
  ssize_t ret = read(fd, &freeze_on_smi, 1);
  if (ret != 1) {
    FATAL() << "Can't read freeze_on_smi";
  }
  if (freeze_on_smi == 0) {
    LOG(warn) << "Failed to read freeze_on_smi";
  } else if (freeze_on_smi == '1') {
    LOG(debug) << "freeze_on_smi is set";
  } else if (freeze_on_smi == '0') {
    LOG(warn) << "freeze_on_smi is not set";
    if (!Flags::get().force_things) {
      CLEAN_FATAL() <<
              "Freezing performance counters on SMIs should be enabled for maximum rr\n"
              "reliability on Comet Lake and later CPUs. To manually enable this setting, run\n"
              "\techo 1 | sudo tee /sys/devices/cpu/freeze_on_smi\n"
              "On systemd systems, consider putting\n"
              "'w /sys/devices/cpu/freeze_on_smi - - - - 1' into /etc/tmpfiles.d/10-rr.conf\n"
              "to automatically apply this setting on every reboot.\n"
              "See 'man 5 sysfs', 'man 5 tmpfiles.d'.\n"
              "If you are seeing this message, the setting has not been enabled.\n";
    } else {
      cpu_improperly_configured = true;
    }
  } else {
    LOG(warn) << "Unrecognized freeze_on_smi value " << freeze_on_smi;
  }
}

// Must be run on the CPU where we're checking for bugs.
static void check_for_arch_bugs(perf_event_attrs &perf_attr) {
  CpuMicroarch uarch = (CpuMicroarch)perf_attr.bug_flags;
  if (uarch >= FirstIntel && uarch <= LastIntel) {
    check_for_kvm_in_txcp_bug(perf_attr);
    check_for_xen_pmi_bug(perf_attr);
  }
  if (uarch >= IntelCometLake && uarch <= LastIntel) {
    check_for_freeze_on_smi();
  }
  if (uarch >= AMDZen && uarch <= LastAMD) {
    check_for_zen_speclockmap();
  }
}

static void post_init_pmu_uarchs(std::vector<PmuConfig> &) {

}

static bool always_recreate_counters(const perf_event_attrs &perf_attr) {
  // When we have the KVM IN_TXCP bug, reenabling the TXCP counter after
  // disabling it does not work.
  DEBUG_ASSERT(perf_attr.checked);
  return perf_attr.has_ioc_period_bug || has_kvm_in_txcp_bug;
}

static void arch_check_restricted_counter() {
  if ((cpuid(CPUID_GETEXTENDEDFEATURES, 0).ebx & HLE_FEATURE_FLAG) &&
    !Flags::get().suppress_environment_warnings) {
    fprintf(stderr,
            "Your CPU supports Hardware Lock Elision but you only have one\n"
            "hardware performance counter available. Record and replay\n"
            "of code that uses HLE will fail unless you alter your\n"
            "configuration to make more than one hardware performance counter\n"
            "available.\n");
  }
}

template <typename Arch>
void PerfCounters::reset_arch_extras(int pmu_index) {
  if (supports_txcp) {
    struct perf_event_attr attr = rr::perf_attrs[pmu_index].ticks;
    if (has_kvm_in_txcp_bug) {
      // IN_TXCP isn't going to work reliably. Assume that HLE/RTM are not
      // used,
      // and check that.
      attr.sample_period = 0;
      attr.config |= IN_TX;
      fd_ticks_in_transaction = start_counter(tid, fd_ticks_interrupt, &attr);
    } else {
      // Set up a separate counter for measuring ticks, which does not have
      // a sample period and does not count events during aborted
      // transactions.
      // We have to use two separate counters here because the kernel does
      // not support setting a sample_period with IN_TXCP, apparently for
      // reasons related to this Intel note on IA32_PERFEVTSEL2:
      // ``When IN_TXCP=1 & IN_TX=1 and in sampling, spurious PMI may
      // occur and transactions may continuously abort near overflow
      // conditions. Software should favor using IN_TXCP for counting over
      // sampling. If sampling, software should use large “sample-after“
      // value after clearing the counter configured to use IN_TXCP and
      // also always reset the counter even when no overflow condition
      // was reported.''
      attr.sample_period = 0;
      attr.config |= IN_TXCP;
      fd_ticks_measure = start_counter(tid, fd_ticks_interrupt, &attr);
    }
  }
}
