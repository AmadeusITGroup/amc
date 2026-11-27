"""GDB pretty-printers for the amc containers: the vectors (vector, SmallVector, FixedCapacityVector, inplace_vector)
and the sets (FlatSet, SmallSet, the iterator of a SmallSet and their node handles).

Usage in GDB:
    source /path/to/amc/tools/gdb/amc_printers.py

Or from ~/.gdbinit:
    python
    import sys
    sys.path.insert(0, "/path/to/amc/tools/gdb")
    import amc_printers
    amc_printers.register_amc_printers(None)
    end
"""

import re
import gdb
import gdb.printing


# Matches amc::<name> and amc::<inline_ns>::<name> (without nested types).
_VECTOR_PREFIX_RE = re.compile(r"^amc::(?:[A-Za-z_][A-Za-z0-9_]*::)?Vector$")
_FLAT_SET_PREFIX_RE = re.compile(r"^amc::(?:[A-Za-z_][A-Za-z0-9_]*::)?FlatSet$")
_SMALL_SET_PREFIX_RE = re.compile(r"^amc::(?:[A-Za-z_][A-Za-z0-9_]*::)?SmallSet$")
_SMALL_SET_ITER_PREFIX_RE = re.compile(
    r"^amc::(?:[A-Za-z_][A-Za-z0-9_]*::)?SmallSetIterator$"
)
_NODE_HANDLE_PREFIX_RE = re.compile(
    r"^amc::(?:[A-Za-z_][A-Za-z0-9_]*::)?set_details::NodeHandle$"
)


def _template_prefix(typename):
    """Return the template name before '<' for a top-level template instantiation, or None."""
    if not typename or not typename.endswith(">"):
        return None
    lt = typename.find("<")
    if lt < 0:
        return None
    depth = 0
    for idx in range(lt, len(typename)):
        ch = typename[idx]
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
            if depth == 0:
                return typename[:lt] if idx == len(typename) - 1 else None
    return None


def _type_prefix(gdb_type):
    """Template name of a struct gdb.Type (see _template_prefix), or None."""
    base_type = gdb_type.strip_typedefs().unqualified()
    return _template_prefix(base_type.tag or base_type.name)


def _find_base(val, prefix_re):
    """The base class subobject of 'val', at any depth, whose template name matches 'prefix_re', with the match."""
    for field in val.type.strip_typedefs().fields():
        if not field.is_base_class:
            continue
        base = val[field]
        match = prefix_re.match(_type_prefix(base.type) or "")
        if match:
            return base, match
        found = _find_base(base, prefix_re)
        if found is not None:
            return found
    return None


def _noun(count):
    return "element" if count == 1 else "elements"


# Storage base classes of amc::Vector (include/amc/vectorcommon.hpp).
_VECTOR_BASE_RE = re.compile(
    r"^amc::(?:[A-Za-z_][A-Za-z0-9_]*::)?vec::(Std|Small|Static)VectorBase$"
)


class _VectorState:
    """Size, capacity and first element of an amc::Vector, read from its storage base class:
    - StdVectorBase (amc::vector): _size, _capa and the pointer _storage;
    - SmallVectorBase (SmallVector): in the inline state, which has no flag, the capacity member holds the size and the
      size member the inline capacity plus one, larger than the size (_capa < _size), and the elements start at
      _storage. Otherwise, the first bytes of _storage hold the pointer to the dynamic storage;
    - StaticVectorBase (FixedCapacityVector, inplace_vector): _size, _capa and the elements from _firstEl.
    The elements after the first one of the inline storages are in a derived class (VectorWithInplaceStorage)."""

    def __init__(self, val):
        self.kind = None
        found = _find_base(val, _VECTOR_BASE_RE)
        if found is None:
            return
        base, match = found
        self.kind = match.group(1)
        elem_ptr_type = val.type.strip_typedefs().unqualified().template_argument(0).pointer()
        size = int(base["_size"])
        capa = int(base["_capa"])
        self.inline = self.kind == "Static"
        if self.kind == "Static":
            self.data = base["_firstEl"].address.cast(elem_ptr_type)
        elif self.kind == "Std":
            self.data = base["_storage"]
        elif capa < size:
            self.inline = True
            size, capa = capa, size - 1
            self.data = base["_storage"].address.cast(elem_ptr_type)
        else:
            self.data = base["_storage"].address.cast(elem_ptr_type.pointer()).dereference()
        self.size = max(size, 0)
        self.capacity = capa

    def elements(self):
        for idx in range(self.size):
            yield (self.data + idx).dereference()


def _std_vector_size(val):
    """Size of a std::vector of libstdc++ or libc++, or None."""
    for first, last in ((("_M_impl", "_M_start"), ("_M_impl", "_M_finish")), (("__begin_",), ("__end_",))):
        try:
            return int(_field_path(val, last) - _field_path(val, first))
        except gdb.error:
            continue
    return None


def _std_set_size(val):
    """Size of a std::set of libstdc++ or libc++, or None."""
    for path in (("_M_t", "_M_impl", "_M_node_count"), ("__tree_", "__size_"), ("__tree_", "__pair3_", "__value_")):
        try:
            return int(_field_path(val, path))
        except gdb.error:
            continue
    return None


def _field_path(val, path):
    """The member of 'val' at 'path', a sequence of names (looked up in the base classes too). Raises gdb.error."""
    for name in path:
        val = val[name]
    return val


def _visualized_children(val):
    """The values of the children of 'val' given by its pretty-printer (libstdc++'s std::set for instance), or None
    without one."""
    visualizer = gdb.default_visualizer(val)
    if visualizer is None or not hasattr(visualizer, "children"):
        return None
    return (child for _, child in visualizer.children())


class _ContainerElements:
    """Size (None if unknown) and elements of a container held by an amc set: an amc vector or set, read by the
    printers of this file, otherwise a std::vector (read here) or another container, read by its pretty-printer."""

    def __init__(self, val):
        self._val = val
        self._kind = None
        self.size = None
        prefix = _type_prefix(val.type)
        if prefix is not None and _VECTOR_PREFIX_RE.match(prefix):
            self._vector = _VectorState(val)
            if self._vector.kind is not None:
                self._kind = "amc_vector"
                self.size = self._vector.size
        elif prefix is not None and _FLAT_SET_PREFIX_RE.match(prefix):
            self._kind = "amc_set"
            self._inner = _ContainerElements(val["_sortedVector"])
            self.size = self._inner.size
        elif prefix in ("std::vector", "std::__1::vector", "std::__debug::vector"):
            self.size = _std_vector_size(val)
            if self.size is not None:
                self._kind = "std_vector"
        else:
            self.size = _std_set_size(val)

    def elements(self):
        if self._kind == "amc_vector":
            yield from self._vector.elements()
        elif self._kind == "amc_set":
            yield from self._inner.elements()
        elif self._kind == "std_vector":
            try:
                data = _field_path(self._val, ("_M_impl", "_M_start"))
            except gdb.error:
                data = self._val["__begin_"]
            for idx in range(self.size):
                yield (data + idx).dereference()
        else:
            children = _visualized_children(self._val)
            if children is None:
                yield self._val  # no pretty-printer: the container as is
            else:
                yield from children

    def empty(self):
        if self.size is not None:
            return self.size == 0
        for _ in self.elements():
            return False
        return True


class VectorPrinter:
    """Pretty-printer for amc::Vector (vector, SmallVector, FixedCapacityVector, inplace_vector)."""

    def __init__(self, val):
        self._val = val
        self._state = _VectorState(val)

    def _container_name(self):
        kind = self._state.kind
        if kind == "Std":
            return "amc::vector"
        if kind == "Small":
            return "amc::SmallVector"
        # inplace_vector is a FixedCapacityVector of BadAllocGrowingPolicy and std::size_t
        vec_type = self._val.type.strip_typedefs().unqualified()
        policy = vec_type.template_argument(3).strip_typedefs()
        size_type = vec_type.template_argument(2).strip_typedefs()
        if (policy.tag or policy.name or "").endswith("BadAllocGrowingPolicy") and size_type.sizeof == gdb.lookup_type(
            "void"
        ).pointer().sizeof:
            return "amc::inplace_vector"
        return "amc::FixedCapacityVector"

    def to_string(self):
        state = self._state
        if state.kind is None:
            return "amc::Vector"
        text = f"{self._container_name()} with {state.size} {_noun(state.size)}, capacity {state.capacity}"
        if state.kind == "Small" and state.inline:
            text += ", inline"
        return text

    def display_hint(self):
        return "array"

    def children(self):
        if self._state.kind is None:
            return
        try:
            for idx, elem in enumerate(self._state.elements()):
                yield (f"[{idx}]", elem)
        except gdb.MemoryError:
            return


class FlatSetPrinter:
    """Pretty-printer for amc::FlatSet: the elements of its sorted vector (an amc vector, a std::vector, or another
    vector type read by its own pretty-printer)."""

    def __init__(self, val):
        self._elems = _ContainerElements(val["_sortedVector"])

    def to_string(self):
        size = self._elems.size
        if size is None:
            return "amc::FlatSet"
        return f"amc::FlatSet with {size} {_noun(size)}"

    def display_hint(self):
        return "array"

    def children(self):
        try:
            for idx, elem in enumerate(self._elems.elements()):
                yield (f"[{idx}]", elem)
        except gdb.MemoryError:
            return


class SmallSetPrinter:
    """Pretty-printer for amc::SmallSet: while its set (_set) is empty, the elements of its inline vector (_vec), in
    insertion order, otherwise the elements of the set (a std::set, a FlatSet...), the vector being then empty."""

    def __init__(self, val):
        self._vec = _VectorState(val["_vec"])
        self._set = _ContainerElements(val["_set"])
        self._inline = self._set.empty()

    def to_string(self):
        if self._inline:
            return f"amc::SmallSet with {self._vec.size} {_noun(self._vec.size)}, inline"
        size = self._set.size
        if size is None:
            return "amc::SmallSet"
        return f"amc::SmallSet with {size} {_noun(size)}"

    def display_hint(self):
        return "array"

    def children(self):
        elems = self._vec.elements() if self._inline else self._set.elements()
        try:
            for idx, elem in enumerate(elems):
                yield (f"[{idx}]", elem)
        except gdb.MemoryError:
            return


def _variant_index_and_address(variant):
    """Index of the alternative of a std::variant of libstdc++ or libc++ and the address of its storage (shared by its
    alternatives), or (None, None)."""
    for index_path, storage_path in ((("_M_index",), ("_M_u",)), (("__impl_", "__index"), ("__impl_", "__data"))):
        try:
            return int(_field_path(variant, index_path)), _field_path(variant, storage_path).address
        except gdb.error:
            continue
    return None, None


class SmallSetIteratorPrinter:
    """Pretty-printer for amc::SmallSetIterator<T, SetIt>, the iterator of a SmallSet whose set iterator is not a
    pointer: a std::variant of an iterator of the set (index 0) and of a pointer to an element of the inline vector
    (index 1). The element of an iterator of the set is printed by the pretty-printer of this iterator, if any."""

    def __init__(self, val):
        it_type = val.type.strip_typedefs().unqualified()
        self._index, address = _variant_index_and_address(val["_iter"])
        self._iterator = None
        if self._index == 0:
            self._iterator = address.cast(it_type.template_argument(1).pointer()).dereference()
        elif self._index == 1:
            elem_ptr_type = it_type.template_argument(0).const().pointer()
            pointer = address.cast(elem_ptr_type.pointer()).dereference()
            if int(pointer) != 0:
                self._iterator = pointer.dereference()

    def to_string(self):
        if self._index == 1:
            return "amc::SmallSetIterator (inline)" if self._iterator is not None else "amc::SmallSetIterator (null)"
        return "amc::SmallSetIterator"

    def children(self):
        if self._iterator is not None:
            yield ("value", self._iterator)


class NodeHandlePrinter:
    """Pretty-printer for amc::set_details::NodeHandle, the node_type of FlatSet and SmallSet: its element, alive if
    and only if the std::optional of its allocator is engaged."""

    def __init__(self, val):
        self._val = val

    def _engaged(self):
        alloc = self._val["_alloc"]
        for path in (("_M_payload", "_M_engaged"), ("__engaged_",)):
            try:
                return bool(int(_field_path(alloc, path)))
            except gdb.error:
                continue
        children = _visualized_children(alloc)
        if children is None:
            return True  # unknown std::optional: the element is printed
        for _ in children:
            return True
        return False

    def to_string(self):
        return "amc::NodeHandle" if self._engaged() else "amc::NodeHandle (empty)"

    def children(self):
        if self._engaged():
            yield ("value", self._val["_s"]["v"])


class _AmcSubprinter(gdb.printing.SubPrettyPrinter):
    def __init__(self, name, prefix_re, printer_cls):
        super().__init__(name)
        self._prefix_re = prefix_re
        self._printer_cls = printer_cls

    def match(self, prefix):
        return self.enabled and self._prefix_re.match(prefix) is not None

    def instantiate(self, val):
        return self._printer_cls(val)


class AmcPrettyPrinter(gdb.printing.PrettyPrinter):
    """Collection of GDB pretty-printers for amc containers."""

    def __init__(self):
        super().__init__("amc", [])
        self.subprinters = [
            _AmcSubprinter("Vector", _VECTOR_PREFIX_RE, VectorPrinter),
            _AmcSubprinter("FlatSet", _FLAT_SET_PREFIX_RE, FlatSetPrinter),
            _AmcSubprinter("SmallSet", _SMALL_SET_PREFIX_RE, SmallSetPrinter),
            _AmcSubprinter("SmallSetIterator", _SMALL_SET_ITER_PREFIX_RE, SmallSetIteratorPrinter),
            _AmcSubprinter("NodeHandle", _NODE_HANDLE_PREFIX_RE, NodeHandlePrinter),
        ]

    def __call__(self, val):
        base_type = val.type.strip_typedefs().unqualified()
        if base_type.code != gdb.TYPE_CODE_STRUCT:
            return None
        typename = base_type.tag or base_type.name
        prefix = _template_prefix(typename)
        if prefix is None:
            return None
        for subprinter in self.subprinters:
            if subprinter.match(prefix):
                return subprinter.instantiate(val)
        return None


def build_pretty_printer():
    return AmcPrettyPrinter()


def register_amc_printers(objfile=None):
    gdb.printing.register_pretty_printer(
        objfile, build_pretty_printer(), replace=True
    )


register_amc_printers(gdb.current_objfile())
