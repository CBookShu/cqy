#pragma once
#include <ylt/easylog.hpp>

namespace cqy {
  #define CQY_TRACE(fmt, ...) MELOGFMT(TRACE, 1, fmt, ##__VA_ARGS__)
  #define CQY_DEBUG(fmt, ...) MELOGFMT(DEBUG, 1, fmt, ##__VA_ARGS__)
  #define CQY_INFO(fmt, ...) MELOGFMT(INFO, 1, fmt, ##__VA_ARGS__)
  #define CQY_WARN(fmt, ...) MELOGFMT(WARN, 1, fmt, ##__VA_ARGS__)
  #define CQY_ERROR(fmt, ...) MELOGFMT(ERROR, 1, fmt, ##__VA_ARGS__)
  #define CQY_CRITICAL(fmt, ...) MELOGFMT(CRITICAL, 1, fmt, ##__VA_ARGS__)
}