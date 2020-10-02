#include "utils/serializer_defs.hpp"
#include "replay.hpp"

SERIALFUNC_PLACEMENT_1(PlayerInput::State,
	SER_FD(is),
	SER_FDT(acts, Array32<SerialTag_Enum< PlayerInput::ACTION_TOTAL_COUNT_INTERNAL >>),
	SER_FD(mov),
	SER_FD(tar_pos),
	SER_FD(cursor));

