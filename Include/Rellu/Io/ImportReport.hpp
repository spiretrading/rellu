#ifndef RELLU_IO_IMPORT_REPORT_HPP
#define RELLU_IO_IMPORT_REPORT_HPP
#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <string>

namespace Rellu {

  /** Describes how an Adobe XD package was imported. */
  struct ImportReport {

    /**
     * Counts the occurrences of every JSON key the import left unconsumed,
     * by the key's normalized path. A path starts with the root it was read
     * from (agc, interactions or a node type such as &lt;shape&gt;), joins
     * keys with dots, writes [] for an array's elements, replaces identifier
     * keys with &lt;id&gt; and ends a scalar with its type, as in
     * &lt;shape&gt;.meta.ux.fixed:bool.
     */
    std::map<std::string, std::size_t, std::less<>> m_unconsumed;

    /** Counts the occurrences of every warning, by message. */
    std::map<std::string, std::size_t, std::less<>> m_warnings;

    /** The number of nodes imported. */
    std::size_t m_node_count = 0;

    /** The time spent reading and parsing the package's entries. */
    std::chrono::nanoseconds m_read_time = std::chrono::nanoseconds(0);

    /** The time spent building the document. */
    std::chrono::nanoseconds m_build_time = std::chrono::nanoseconds(0);
  };
}

#endif
