#ifndef _NEXUS__INC__COMPILER_CHECK_H_
#define _NEXUS__INC__COMPILER_CHECK_H_

// COMPILER-SPECIFIC MACROS
//
// The below options ensure that the 'packed' directly is correctly
// implemented based on the compiler in use. This section likely
// does not need to be modified, *unless* your preferred compiler is not
// detected below.
//
// **Warning** - commenting out or removing this section will prevent proper
// operation of the system.
//

// Keil/ARMCC
#if defined(__ARMCC_VERSION)
#define NEXUS_PACKED_STRUCT __packed struct
#define NEXUS_PACKED_UNION __packed union
// GNU gcc
#elif defined(__GNUC__)
#define NEXUS_PACKED_STRUCT struct __attribute__((packed))
#define NEXUS_PACKED_UNION union __attribute__((packed))
// IAR ICC
#elif defined(__IAR_SYSTEMS_ICC__)
#define NEXUS_PACKED_STRUCT __packed struct
#define NEXUS_PACKED_UNION __packed union
#else
#ifndef NEXUS_PACKED_STRUCT
#error                                                                         \
    "Unrecognized compiler. Defaults unavailable. Please define NEXUS_PACKED_STRUCT."
#endif
#ifndef NEXUS_PACKED_UNION
#error                                                                         \
    "Unrecognized compiler. Defaults unavailable. Please define NEXUS_PACKED_UNION."
#endif
#endif

#endif /* end of include guard: _NEXUS__INC__COMPILER_CHECK_H_ */
