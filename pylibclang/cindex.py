# ===- cindex.py - Python Indexing Library Bindings -----------*- python -*--===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===#

r"""
Clang Indexing Library Bindings
===============================

This module provides an interface to the Clang indexing library. It is a
low-level interface to the indexing library which attempts to match the Clang
API directly while also being "pythonic". Notable differences from the C API
are:

 * string results are returned as Python strings, not CXString objects.

 * null cursors are translated to None.

 * access to child cursors is done via iteration, not visitation.

The major indexing objects are:

  Index

    The top-level object which manages some global library state.

  TranslationUnit

    High-level object encapsulating the AST for a single translation unit. These
    can be loaded from .ast files or parsed on the fly.

  Cursor

    Generic object for representing a node in the AST.

  SourceRange, SourceLocation, and File

    Objects representing information about the input source.

Most object information is exposed using properties, when the underlying API
call is efficient.
"""
from __future__ import absolute_import, division, print_function

import os
import sys
import functools
import inspect

from pylibclang import _C


class _Conf:
    """Only used to keep compatibility with old code."""
    pass


conf = _Conf()
conf.lib = _C

# Importing ABC-s directly from collections is deprecated since Python 3.7,
# will stop working in Python 3.8.
# See: https://docs.python.org/dev/whatsnew/3.7.html#id3
if sys.version_info[:2] >= (3, 7):
    from collections import abc as collections_abc
else:
    import collections as collections_abc

# We only support PathLike objects on Python version with os.fspath present
# to be consistent with the Python standard library. On older Python versions
# we only support strings and we have dummy fspath to just pass them through.
try:
    fspath = os.fspath
except AttributeError:

    def fspath(x):
        return x


### Exception Classes ###


class TranslationUnitLoadError(Exception):
    """Represents an error that occurred when loading a TranslationUnit.

    This is raised in the case where a TranslationUnit could not be
    instantiated due to failure in the libclang library.

    FIXME: Make libclang expose additional error information in this scenario.
    """

    pass


class TranslationUnitSaveError(Exception):
    """Represents an error that occurred when saving a TranslationUnit.

    Each error has associated with it an enumerated value, accessible under
    e.save_error. Consumers can compare the value with one of the ERROR_
    constants in this class.
    """

    # Indicates that an unknown error occurred. This typically indicates that
    # I/O failed during save.
    ERROR_UNKNOWN = 1

    # Indicates that errors during translation prevented saving. The errors
    # should be available via the TranslationUnit's diagnostics.
    ERROR_TRANSLATION_ERRORS = 2

    # Indicates that the translation unit was somehow invalid.
    ERROR_INVALID_TU = 3

    def __init__(self, enumeration, message):
        assert isinstance(enumeration, int)

        if enumeration < 1 or enumeration > 3:
            raise Exception(
                "Encountered undefined TranslationUnit save error "
                "constant: %d. Please file a bug to have this "
                "value supported." % enumeration
            )

        self.save_error = enumeration
        Exception.__init__(self, "Error %d: %s" % (enumeration, message))


### Structures and Utility Classes ###


class CachedProperty(object):
    """Decorator that lazy-loads the value of a property.

    The first time the property is accessed, the original property function is
    executed. The value it returns is set as the new value of that instance's
    property, replacing the original method.
    """

    def __init__(self, wrapped):
        self.wrapped = wrapped
        try:
            self.__doc__ = wrapped.__doc__
        except:
            pass

    def __get__(self, instance, instance_type=None):
        if instance is None:
            return self

        value = self.wrapped(instance)
        setattr(instance, self.wrapped.__name__, value)

        return value


def _get_user_attributes(cls):
    boring = dir(type('dummy', (object,), {}))
    return [item
            for item in inspect.getmembers(cls)
            if item[0] not in boring]


def _enhance(target_type):
    def deco(src_type):
        """Inject all newly defined methods from src_type into target_type"""
        for name, attr in _get_user_attributes(src_type):
            setattr(target_type, name, attr)
        return target_type

    return deco


def _result_as(cls):
    def deco(func):
        """Convert the result of func to cls"""

        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            return cls.from_result(func(*args, **kwargs), func, args)

        return wrapper

    return deco


@_enhance(_C.CXString)
class _CXString:
    """Helper for transforming CXString results."""

    def __del__(self):
        conf.lib.clang_disposeString(self)

    @staticmethod
    def from_result(res, fn=None, args=None):
        assert isinstance(res, _CXString)
        return conf.lib.clang_getCString(res)

    def __str__(self):
        return conf.lib.clang_getCString(self)


@_enhance(_C.CXSourceLocation)
class SourceLocation:
    """
    A SourceLocation represents a particular location within a source file.
    """

    @functools.cache
    def _get_instantiation(self):
        f, l, c, o = conf.lib.clang_getInstantiationLocation(self)
        return File(f), l, c, o

    @staticmethod
    def from_position(tu, file, line, column):
        """
        Retrieve the source location associated with a given file/line/column in
        a particular translation unit.
        """
        return conf.lib.clang_getLocation(tu, file, line, column)

    @staticmethod
    def from_offset(tu, file, offset):
        """Retrieve a SourceLocation from a given character offset.

        tu -- TranslationUnit file belongs to
        file -- File instance to obtain offset from
        offset -- Integer character offset within file
        """
        return conf.lib.clang_getLocationForOffset(tu, file, offset)

    @property
    def file(self):
        """Get the file represented by this source location."""
        return self._get_instantiation()[0]

    @property
    def line(self):
        """Get the line represented by this source location."""
        return self._get_instantiation()[1]

    @property
    def column(self):
        """Get the column represented by this source location."""
        return self._get_instantiation()[2]

    @property
    def offset(self):
        """Get the file offset represented by this source location."""
        return self._get_instantiation()[3]

    @property
    def is_in_system_header(self):
        """Returns true if the given source location is in a system header."""
        return conf.lib.clang_Location_isInSystemHeader(self)

    def __eq__(self, other):
        return conf.lib.clang_equalLocations(self, other)

    def __ne__(self, other):
        return not self.__eq__(other)

    def __repr__(self):
        if self.file:
            filename = self.file.name
        else:
            filename = None
        return "<SourceLocation file %r, line %r, column %r>" % (
            filename,
            self.line,
            self.column,
        )


@_enhance(_C.CXSourceRange)
class SourceRange:
    """
    A SourceRange describes a range of source locations within the source
    code.
    """

    @staticmethod
    def from_locations(start, end):
        return conf.lib.clang_getRange(start, end)

    @property
    def start(self):
        """
        Return a SourceLocation representing the first character within a
        source range.
        """
        return conf.lib.clang_getRangeStart(self)

    @property
    def end(self):
        """
        Return a SourceLocation representing the last character within a
        source range.
        """
        return conf.lib.clang_getRangeEnd(self)

    def __eq__(self, other):
        return conf.lib.clang_equalRanges(self, other)

    def __ne__(self, other):
        return not self.__eq__(other)

    def __contains__(self, other):
        """Useful to detect the Token/Lexer bug"""
        if not isinstance(other, SourceLocation):
            return False
        if other.file is None and self.start.file is None:
            pass
        elif (
                self.start.file.name != other.file.name
                or other.file.name != self.end.file.name
        ):
            # same file name
            return False
        # same file, in between lines
        if self.start.line < other.line < self.end.line:
            return True
        elif self.start.line == other.line:
            # same file first line
            if self.start.column <= other.column:
                return True
        elif other.line == self.end.line:
            # same file last line
            if other.column <= self.end.column:
                return True
        return False

    def __repr__(self):
        return "<SourceRange start %r, end %r>" % (self.start, self.end)


class ClangObject(_C.voidp):

    def __new__(cls, obj):
        if obj is None:
            return None
        return _C.voidp.__new__(cls)

    def __init__(self, obj):
        _C.voidp.__init__(self, obj.get_ptr())


class Diagnostic(ClangObject):
    """
    A Diagnostic is a single instance of a Clang diagnostic. It includes the
    diagnostic severity, the message, the location the diagnostic occurred, as
    well as additional source ranges and associated fix-it hints.
    """

    Ignored = 0
    Note = 1
    Warning = 2
    Error = 3
    Fatal = 4

    DisplaySourceLocation = 0x01
    DisplayColumn = 0x02
    DisplaySourceRanges = 0x04
    DisplayOption = 0x08
    DisplayCategoryId = 0x10
    DisplayCategoryName = 0x20
    _FormatOptionsMask = 0x3F

    def __del__(self):
        conf.lib.clang_disposeDiagnostic(self)

    @property
    def severity(self):
        return conf.lib.clang_getDiagnosticSeverity(self)

    @property
    def location(self):
        return conf.lib.clang_getDiagnosticLocation(self)

    @property
    def spelling(self):
        return conf.lib.clang_getDiagnosticSpelling(self)

    @property
    def ranges(self):
        class RangeIterator(object):
            def __init__(self, diag):
                self.diag = diag

            def __len__(self):
                return int(conf.lib.clang_getDiagnosticNumRanges(self.diag))

            def __getitem__(self, key):
                if key >= len(self):
                    raise IndexError
                return conf.lib.clang_getDiagnosticRange(self.diag, key)

        return RangeIterator(self)

    @property
    def fixits(self):
        class FixItIterator(object):
            def __init__(self, diag):
                self.diag = diag

            def __len__(self):
                return int(conf.lib.clang_getDiagnosticNumFixIts(self.diag))

            def __getitem__(self, key):
                range = SourceRange()
                value = conf.lib.clang_getDiagnosticFixIt(self.diag, key, range)
                if len(value) == 0:
                    raise IndexError

                return FixIt(range, value)

        return FixItIterator(self)

    @property
    def children(self):
        class ChildDiagnosticsIterator(object):
            def __init__(self, diag):
                self.diag_set = conf.lib.clang_getChildDiagnostics(diag)

            def __len__(self):
                return int(conf.lib.clang_getNumDiagnosticsInSet(self.diag_set))

            def __getitem__(self, key):
                diag = conf.lib.clang_getDiagnosticInSet(self.diag_set, key)
                if not diag:
                    raise IndexError
                return Diagnostic(diag)

        return ChildDiagnosticsIterator(self)

    @property
    def category_number(self):
        """The category number for this diagnostic or 0 if unavailable."""
        return conf.lib.clang_getDiagnosticCategory(self)

    @property
    def category_name(self):
        """The string name of the category for this diagnostic."""
        return conf.lib.clang_getDiagnosticCategoryText(self)

    @property
    def option(self):
        """The command-line option that enables this diagnostic."""
        return conf.lib.clang_getDiagnosticOption(self, None)

    @property
    def disable_option(self):
        """The command-line option that disables this diagnostic."""
        disable = _CXString()
        conf.lib.clang_getDiagnosticOption(self, disable)
        return _CXString.from_result(disable)

    def format(self, options=None):
        """
        Format this diagnostic for display. The options argument takes
        Diagnostic.Display* flags, which can be combined using bitwise OR. If
        the options argument is not provided, the default display options will
        be used.
        """
        if options is None:
            options = conf.lib.clang_defaultDiagnosticDisplayOptions()
        if options & ~Diagnostic._FormatOptionsMask:
            raise ValueError("Invalid format options")
        return conf.lib.clang_formatDiagnostic(self, options)

    def __repr__(self):
        return "<Diagnostic severity %r, location %r, spelling %r>" % (
            self.severity,
            self.location,
            self.spelling,
        )

    def __str__(self):
        return self.format()


class FixIt(object):
    """
    A FixIt represents a transformation to be applied to the source to
    "fix-it". The fix-it shouldbe applied by replacing the given source range
    with the given value.
    """

    def __init__(self, range, value):
        self.range = range
        self.value = value

    def __repr__(self):
        return "<FixIt range %r, value %r>" % (self.range, self.value)


class TokenGroup(object):
    """Helper class to facilitate token management.

    Tokens are allocated from libclang in chunks. They must be disposed of as a
    collective group.

    One purpose of this class is for instances to represent groups of allocated
    tokens. Each token in a group contains a reference back to an instance of
    this class. When all tokens from a group are garbage collected, it allows
    this class to be garbage collected. When this class is garbage collected,
    it calls the libclang destructor which invalidates all tokens in the group.

    You should not instantiate this class outside of this module.
    """

    def __init__(self, tu, memory, count):
        self._tu = tu
        self._first_token = memory
        self._count = count

    def __del__(self):
        conf.lib.clang_disposeTokens(self._tu, self._first_token, self._count)

    @staticmethod
    def get_tokens(tu, extent):
        """Helper method to return all tokens in an extent.

        This functionality is needed multiple places in this module. We define
        it here because it seems like a logical place.
        """

        tokens = conf.lib.clang_tokenize(tu, extent)
        count = tokens.n

        # If we get no tokens, no memory was allocated. Be sure not to return
        # anything and potentially call a destructor on nothing.
        if count < 1:
            return

        token_group = TokenGroup(tu, tokens.at(0), count)

        for i in range(0, count):
            token = tokens.at(i)
            token._tu = tu
            token._group = token_group

            yield token


TokenKind = _C.CXTokenKind


@_enhance(_C.CXCursorKind)
class CursorKind:
    """
    A CursorKind describes the kind of entity that a cursor points to.
    """

    def is_declaration(self):
        """Test if this is a declaration kind."""
        return conf.lib.clang_isDeclaration(self)

    def is_reference(self):
        """Test if this is a reference kind."""
        return conf.lib.clang_isReference(self)

    def is_expression(self):
        """Test if this is an expression kind."""
        return conf.lib.clang_isExpression(self)

    def is_statement(self):
        """Test if this is a statement kind."""
        return conf.lib.clang_isStatement(self)

    def is_attribute(self):
        """Test if this is an attribute kind."""
        return conf.lib.clang_isAttribute(self)

    def is_invalid(self):
        """Test if this is an invalid kind."""
        return conf.lib.clang_isInvalid(self)

    def is_translation_unit(self):
        """Test if this is a translation unit kind."""
        return conf.lib.clang_isTranslationUnit(self)

    def is_preprocessing(self):
        """Test if this is a preprocessing kind."""
        return conf.lib.clang_isPreprocessing(self)

    def is_unexposed(self):
        """Test if this is an unexposed kind."""
        return conf.lib.clang_isUnexposed(self)


TemplateArgumentKind = _C.CXTemplateArgumentKind
ExceptionSpecificationKind = _C.CXCursor_ExceptionSpecificationKind


### Cursors ###


@_enhance(_C.CXCursor)
class Cursor:
    """
    The Cursor class represents a reference to an element within the AST. It
    acts as a kind of iterator.
    """

    @staticmethod
    def from_location(tu, location):
        # We store a reference to the TU in the instance so the TU won't get
        # collected before the cursor.
        cursor = conf.lib.clang_getCursor(tu, location)
        cursor._tu = tu

        return cursor

    def __eq__(self, other):
        return conf.lib.clang_equalCursors(self, other)

    def __ne__(self, other):
        return not self.__eq__(other)

    def is_definition(self):
        """
        Returns true if the declaration pointed at by the cursor is also a
        definition of that entity.
        """
        return conf.lib.clang_isCursorDefinition(self)

    def is_const_method(self):
        """Returns True if the cursor refers to a C++ member function or member
        function template that is declared 'const'.
        """
        return conf.lib.clang_CXXMethod_isConst(self)

    def is_converting_constructor(self):
        """Returns True if the cursor refers to a C++ converting constructor."""
        return conf.lib.clang_CXXConstructor_isConvertingConstructor(self)

    def is_copy_constructor(self):
        """Returns True if the cursor refers to a C++ copy constructor."""
        return conf.lib.clang_CXXConstructor_isCopyConstructor(self)

    def is_default_constructor(self):
        """Returns True if the cursor refers to a C++ default constructor."""
        return conf.lib.clang_CXXConstructor_isDefaultConstructor(self)

    def is_move_constructor(self):
        """Returns True if the cursor refers to a C++ move constructor."""
        return conf.lib.clang_CXXConstructor_isMoveConstructor(self)

    def is_default_method(self):
        """Returns True if the cursor refers to a C++ member function or member
        function template that is declared '= default'.
        """
        return conf.lib.clang_CXXMethod_isDefaulted(self)

    def is_deleted_method(self):
        """Returns True if the cursor refers to a C++ member function or member
        function template that is declared '= delete'.
        """
        return conf.lib.clang_CXXMethod_isDeleted(self)

    def is_copy_assignment_operator_method(self):
        """Returnrs True if the cursor refers to a copy-assignment operator.

        A copy-assignment operator `X::operator=` is a non-static,
        non-template member function of _class_ `X` with exactly one
        parameter of type `X`, `X&`, `const X&`, `volatile X&` or `const
        volatile X&`.


        That is, for example, the `operator=` in:

           class Foo {
               bool operator=(const volatile Foo&);
           };

        Is a copy-assignment operator, while the `operator=` in:

           class Bar {
               bool operator=(const int&);
           };

        Is not.
        """
        return conf.lib.clang_CXXMethod_isCopyAssignmentOperator(self)

    def is_move_assignment_operator_method(self):
        """Returnrs True if the cursor refers to a move-assignment operator.

        A move-assignment operator `X::operator=` is a non-static,
        non-template member function of _class_ `X` with exactly one
        parameter of type `X&&`, `const X&&`, `volatile X&&` or `const
        volatile X&&`.


        That is, for example, the `operator=` in:

           class Foo {
               bool operator=(const volatile Foo&&);
           };

        Is a move-assignment operator, while the `operator=` in:

           class Bar {
               bool operator=(const int&&);
           };

        Is not.
        """
        return conf.lib.clang_CXXMethod_isMoveAssignmentOperator(self)

    def is_explicit_method(self):
        """Determines if a C++ constructor or conversion function is
        explicit, returning 1 if such is the case and 0 otherwise.

        Constructors or conversion functions are declared explicit through
        the use of the explicit specifier.

        For example, the following constructor and conversion function are
        not explicit as they lack the explicit specifier:

            class Foo {
                Foo();
                operator int();
            };

        While the following constructor and conversion function are
        explicit as they are declared with the explicit specifier.

            class Foo {
                explicit Foo();
                explicit operator int();
            };

        This method will return 0 when given a cursor pointing to one of
        the former declarations and it will return 1 for a cursor pointing
        to the latter declarations.

        The explicit specifier allows the user to specify a
        conditional compile-time expression whose value decides
        whether the marked element is explicit or not.

        For example:

            constexpr bool foo(int i) { return i % 2 == 0; }

            class Foo {
                 explicit(foo(1)) Foo();
                 explicit(foo(2)) operator int();
            }

        This method will return 0 for the constructor and 1 for
        the conversion function.
        """
        return conf.lib.clang_CXXMethod_isExplicit(self)

    def is_mutable_field(self):
        """Returns True if the cursor refers to a C++ field that is declared
        'mutable'.
        """
        return conf.lib.clang_CXXField_isMutable(self)

    def is_pure_virtual_method(self):
        """Returns True if the cursor refers to a C++ member function or member
        function template that is declared pure virtual.
        """
        return conf.lib.clang_CXXMethod_isPureVirtual(self)

    def is_static_method(self):
        """Returns True if the cursor refers to a C++ member function or member
        function template that is declared 'static'.
        """
        return conf.lib.clang_CXXMethod_isStatic(self)

    def is_virtual_method(self):
        """Returns True if the cursor refers to a C++ member function or member
        function template that is declared 'virtual'.
        """
        return conf.lib.clang_CXXMethod_isVirtual(self)

    def is_abstract_record(self):
        """Returns True if the cursor refers to a C++ record declaration
        that has pure virtual member functions.
        """
        return conf.lib.clang_CXXRecord_isAbstract(self)

    def is_scoped_enum(self):
        """Returns True if the cursor refers to a scoped enum declaration."""
        return conf.lib.clang_EnumDecl_isScoped(self)

    def get_definition(self):
        """
        If the cursor is a reference to a declaration or a declaration of
        some entity, return a cursor that points to the definition of that
        entity.
        """
        # TODO: Should probably check that this is either a reference or
        # declaration prior to issuing the lookup.
        return conf.lib.clang_getCursorDefinition(self)

    def get_usr(self):
        """Return the Unified Symbol Resolution (USR) for the entity referenced
        by the given cursor (or None).

        A Unified Symbol Resolution (USR) is a string that identifies a
        particular entity (function, class, variable, etc.) within a
        program. USRs can be compared across translation units to determine,
        e.g., when references in one translation refer to an entity defined in
        another translation unit."""
        return conf.lib.clang_getCursorUSR(self)

    def get_included_file(self):
        """Returns the File that is included by the current inclusion cursor."""
        assert self.kind == CursorKind.CXCursor_InclusionDirective

        return conf.lib.clang_getIncludedFile(self)

    @property
    def spelling(self):
        """Return the spelling of the entity pointed at by the cursor."""
        if not hasattr(self, "_spelling"):
            self._spelling = conf.lib.clang_getCursorSpelling(self)

        return self._spelling

    @property
    def displayname(self):
        """
        Return the display name for the entity referenced by this cursor.

        The display name contains extra information that helps identify the
        cursor, such as the parameters of a function or template or the
        arguments of a class template specialization.
        """
        if not hasattr(self, "_displayname"):
            self._displayname = conf.lib.clang_getCursorDisplayName(self)

        return self._displayname

    @property
    def mangled_name(self):
        """Return the mangled name for the entity referenced by this cursor."""
        if not hasattr(self, "_mangled_name"):
            self._mangled_name = conf.lib.clang_Cursor_getMangling(self)

        return self._mangled_name

    @property
    @functools.cache
    def location(self):
        """
        Return the source location (the starting character) of the entity
        pointed at by the cursor.
        """

        return conf.lib.clang_getCursorLocation(self)

    @property
    @functools.cache
    def linkage(self):
        """Return the linkage of this cursor."""
        return conf.lib.clang_getCursorLinkage(self)

    @property
    @functools.cache
    def tls_kind(self):
        """Return the thread-local storage (TLS) kind of this cursor."""
        return conf.lib.clang_getCursorTLSKind(self)

    @property
    def extent(self):
        """
        Return the source range (the range of text) occupied by the entity
        pointed at by the cursor.
        """
        if not hasattr(self, "_extent"):
            self._extent = conf.lib.clang_getCursorExtent(self)

        return self._extent

    @property
    @functools.cache
    def storage_class(self):
        """
        Retrieves the storage class (if any) of the entity pointed at by the
        cursor.
        """
        return conf.lib.clang_Cursor_getStorageClass(self)

    @property
    @functools.cache
    def availability(self):
        """
        Retrieves the availability of the entity pointed at by the cursor.
        """
        return conf.lib.clang_getCursorAvailability(self)

    @property
    @functools.cache
    def access_specifier(self):
        """
        Retrieves the access specifier (if any) of the entity pointed at by the
        cursor.
        """

        return conf.lib.clang_getCXXAccessSpecifier(self)

    @property
    @functools.cache
    def type(self):
        """
        Retrieve the Type (if any) of the entity pointed at by the cursor.
        """
        return conf.lib.clang_getCursorType(self)

    @property
    def canonical(self):
        """Return the canonical Cursor corresponding to this Cursor.

        The canonical cursor is the cursor which is representative for the
        underlying entity. For example, if you have multiple forward
        declarations for the same class, the canonical cursor for the forward
        declarations will be identical.
        """
        if not hasattr(self, "_canonical"):
            self._canonical = conf.lib.clang_getCanonicalCursor(self)

        return self._canonical

    @property
    def result_type(self):
        """Retrieve the Type of the result for this Cursor."""
        if not hasattr(self, "_result_type"):
            self._result_type = conf.lib.clang_getCursorResultType(self)

        return self._result_type

    @property
    @functools.cache
    def exception_specification_kind(self):
        """
        Retrieve the exception specification kind, which is one of the values
        from the ExceptionSpecificationKind enumeration.
        """
        return conf.lib.clang_getCursorExceptionSpecificationType(self)

    @property
    @functools.cache
    def underlying_typedef_type(self):
        """Return the underlying type of a typedef declaration.

        Returns a Type for the typedef this cursor is a declaration for. If
        the current cursor is not a typedef, this raises.
        """
        assert self.kind.is_declaration()
        return conf.lib.clang_getTypedefDeclUnderlyingType(self)

    @property
    def enum_type(self):
        """Return the integer type of an enum declaration.

        Returns a Type corresponding to an integer. If the cursor is not for an
        enum, this raises.
        """
        assert self.kind == CursorKind.CXCursor_EnumDecl
        return conf.lib.clang_getEnumDeclIntegerType(self)

    @property
    def enum_value(self):
        """Return the value of an enum constant."""
        if not hasattr(self, "_enum_value"):
            assert self.kind == CursorKind.CXCursor_EnumConstantDecl
            # Figure out the underlying type of the enum to know if it
            # is a signed or unsigned quantity.
            underlying_type = self.type
            if underlying_type.kind == TypeKind.CXType_Enum:
                underlying_type = underlying_type.get_declaration().enum_type
            if underlying_type.kind in (
                    TypeKind.CXType_Char_U,
                    TypeKind.CXType_UChar,
                    TypeKind.CXType_Char16,
                    TypeKind.CXType_Char32,
                    TypeKind.CXType_UShort,
                    TypeKind.CXType_UInt,
                    TypeKind.CXType_ULong,
                    TypeKind.CXType_ULongLong,
                    TypeKind.CXType_UInt128,
            ):
                self._enum_value = conf.lib.clang_getEnumConstantDeclUnsignedValue(self)
            else:
                self._enum_value = conf.lib.clang_getEnumConstantDeclValue(self)
        return self._enum_value

    @property
    def objc_type_encoding(self):
        """Return the Objective-C type encoding as a str."""
        if not hasattr(self, "_objc_type_encoding"):
            self._objc_type_encoding = conf.lib.clang_getDeclObjCTypeEncoding(self)

        return self._objc_type_encoding

    @property
    def hash(self):
        """Returns a hash of the cursor as an int."""
        if not hasattr(self, "_hash"):
            self._hash = conf.lib.clang_hashCursor(self)

        return self._hash

    @property
    def semantic_parent(self):
        """Return the semantic parent for this cursor."""
        if not hasattr(self, "_semantic_parent"):
            self._semantic_parent = conf.lib.clang_getCursorSemanticParent(self)

        return self._semantic_parent

    @property
    def lexical_parent(self):
        """Return the lexical parent for this cursor."""
        if not hasattr(self, "_lexical_parent"):
            self._lexical_parent = conf.lib.clang_getCursorLexicalParent(self)

        return self._lexical_parent

    @property
    def translation_unit(self):
        """Returns the TranslationUnit to which this Cursor belongs."""
        # If this triggers an AttributeError, the instance was not properly
        # created.
        return self._tu

    @property
    def referenced(self):
        """
        For a cursor that is a reference, returns a cursor
        representing the entity that it references.
        """
        if not hasattr(self, "_referenced"):
            self._referenced = conf.lib.clang_getCursorReferenced(self)

        return self._referenced

    @property
    def brief_comment(self):
        """Returns the brief comment text associated with that Cursor"""
        return conf.lib.clang_Cursor_getBriefCommentText(self)

    @property
    def raw_comment(self):
        """Returns the raw comment text associated with that Cursor"""
        return conf.lib.clang_Cursor_getRawCommentText(self)

    def get_arguments(self):
        """Return an iterator for accessing the arguments of this cursor."""
        num_args = conf.lib.clang_Cursor_getNumArguments(self)
        for i in range(0, num_args):
            yield conf.lib.clang_Cursor_getArgument(self, i)

    def get_num_template_arguments(self):
        """Returns the number of template args associated with this cursor."""
        return conf.lib.clang_Cursor_getNumTemplateArguments(self)

    def get_template_argument_kind(self, num):
        """Returns the TemplateArgumentKind for the indicated template
        argument."""
        return conf.lib.clang_Cursor_getTemplateArgumentKind(self, num)

    def get_template_argument_type(self, num):
        """Returns the CXType for the indicated template argument."""
        return conf.lib.clang_Cursor_getTemplateArgumentType(self, num)

    def get_template_argument_value(self, num):
        """Returns the value of the indicated arg as a signed 64b integer."""
        return conf.lib.clang_Cursor_getTemplateArgumentValue(self, num)

    def get_template_argument_unsigned_value(self, num):
        """Returns the value of the indicated arg as an unsigned 64b integer."""
        return conf.lib.clang_Cursor_getTemplateArgumentUnsignedValue(self, num)

    def get_children(self):
        """Return an iterator for accessing the children of this cursor."""

        # FIXME: Expose iteration from CIndex, PR6125.
        children = []

        def visitor(child, parent, data):
            # FIXME: Document this assertion in API.
            # FIXME: There should just be an isNull method.
            assert child != conf.lib.clang_getNullCursor()

            # Create reference to TU so it isn't GC'd before Cursor.
            child._tu = self._tu
            child._ast_parent = parent
            child._ast_parent._tu = self._tu
            children.append(child)
            return _C.CXChildVisitResult.CXChildVisit_Continue

        conf.lib.clang_visitChildren(self, visitor, _C.voidp(0))
        return iter(children)

    def walk_preorder(self):
        """Depth-first preorder walk over the cursor and its descendants.

        Yields cursors.
        """
        yield self
        for child in self.get_children():
            for descendant in child.walk_preorder():
                yield descendant

    def get_tokens(self):
        """Obtain Token instances formulating that compose this Cursor.

        This is a generator for Token instances. It returns all tokens which
        occupy the extent this cursor occupies.
        """
        return TokenGroup.get_tokens(self._tu, self.extent)

    def get_field_offsetof(self):
        """Returns the offsetof the FIELD_DECL pointed by this Cursor."""
        return conf.lib.clang_Cursor_getOffsetOfField(self)

    def is_anonymous(self):
        """
        Check if the record is anonymous.
        """
        if self.kind == CursorKind.CXCursor_FieldDecl:
            return self.type.get_declaration().is_anonymous()
        return conf.lib.clang_Cursor_isAnonymous(self)

    def is_bitfield(self):
        """
        Check if the field is a bitfield.
        """
        return conf.lib.clang_Cursor_isBitField(self)

    def get_bitfield_width(self):
        """
        Retrieve the width of a bitfield.
        """
        return conf.lib.clang_getFieldDeclBitWidth(self)

    @staticmethod
    def from_result(res, fn, args):
        assert isinstance(res, Cursor)
        # FIXME: There should just be an isNull method.
        if res == conf.lib.clang_getNullCursor():
            return None

        # Store a reference to the TU in the Python object so it won't get GC'd
        # before the Cursor.
        tu = None
        for arg in args:
            if isinstance(arg, TranslationUnit):
                tu = arg
                break

            if hasattr(arg, "translation_unit"):
                tu = arg.translation_unit
                break

        assert tu is not None

        res._tu = tu
        return res

    @staticmethod
    def from_cursor_result(res, fn, args):
        assert isinstance(res, Cursor)
        if res == conf.lib.clang_getNullCursor():
            return None

        res._tu = args[0]._tu
        return res


StorageClass = _C.CX_StorageClass
AvailabilityKind = _C.CXAvailabilityKind
AccessSpecifier = _C.CX_CXXAccessSpecifier
TypeKind = _C.CXTypeKind
RefQualifierKind = _C.CXRefQualifierKind
LinkageKind = _C.CXLinkageKind
TLSKind = _C.CXTLSKind


@_enhance(_C.CXType)
class Type:
    """
    The type of an element in the abstract syntax tree.
    """

    def argument_types(self):
        """Retrieve a container for the non-variadic arguments for this type.

        The returned object is iterable and indexable. Each item in the
        container is a Type instance.
        """

        class ArgumentsIterator(collections_abc.Sequence):
            def __init__(self, parent):
                self.parent = parent
                self.length = None

            def __len__(self):
                if self.length is None:
                    self.length = conf.lib.clang_getNumArgTypes(self.parent)

                return self.length

            def __getitem__(self, key):
                # FIXME Support slice objects.
                if not isinstance(key, int):
                    raise TypeError("Must supply a non-negative int.")

                if key < 0:
                    raise IndexError("Only non-negative indexes are accepted.")

                if key >= len(self):
                    raise IndexError(
                        "Index greater than container length: "
                        "%d > %d" % (key, len(self))
                    )

                result = conf.lib.clang_getArgType(self.parent, key)
                if result.kind == TypeKind.CXType_Invalid:
                    raise IndexError("Argument could not be retrieved.")

                return result

        assert self.kind == TypeKind.CXType_FunctionProto
        return ArgumentsIterator(self)

    @property
    def element_type(self):
        """Retrieve the Type of elements within this Type.

        If accessed on a type that is not an array, complex, or vector type, an
        exception will be raised.
        """
        result = conf.lib.clang_getElementType(self)
        if result.kind == TypeKind.CXType_Invalid:
            raise Exception("Element type not available on this type.")

        return result

    @property
    def element_count(self):
        """Retrieve the number of elements in this type.

        Returns an int.

        If the Type is not an array or vector, this raises.
        """
        result = conf.lib.clang_getNumElements(self)
        if result < 0:
            raise Exception("Type does not have elements.")

        return result

    @property
    def translation_unit(self):
        """The TranslationUnit to which this Type is associated."""
        # If this triggers an AttributeError, the instance was not properly
        # instantiated.
        return self._tu

    @staticmethod
    def from_result(res, fn, args):
        assert isinstance(res, Type)

        tu = None
        for arg in args:
            if hasattr(arg, "translation_unit"):
                tu = arg.translation_unit
                break

        assert tu is not None
        res._tu = tu

        return res

    def get_num_template_arguments(self):
        return conf.lib.clang_Type_getNumTemplateArguments(self)

    def get_template_argument_type(self, num):
        return conf.lib.clang_Type_getTemplateArgumentAsType(self, num)

    def get_canonical(self):
        """
        Return the canonical type for a Type.

        Clang's type system explicitly models typedefs and all the
        ways a specific type can be represented.  The canonical type
        is the underlying type with all the "sugar" removed.  For
        example, if 'T' is a typedef for 'int', the canonical type for
        'T' would be 'int'.
        """
        return conf.lib.clang_getCanonicalType(self)

    def is_const_qualified(self):
        """Determine whether a Type has the "const" qualifier set.

        This does not look through typedefs that may have added "const"
        at a different level.
        """
        return conf.lib.clang_isConstQualifiedType(self)

    def is_volatile_qualified(self):
        """Determine whether a Type has the "volatile" qualifier set.

        This does not look through typedefs that may have added "volatile"
        at a different level.
        """
        return conf.lib.clang_isVolatileQualifiedType(self)

    def is_restrict_qualified(self):
        """Determine whether a Type has the "restrict" qualifier set.

        This does not look through typedefs that may have added "restrict" at
        a different level.
        """
        return conf.lib.clang_isRestrictQualifiedType(self)

    def is_function_variadic(self):
        """Determine whether this function Type is a variadic function type."""
        assert self.kind == TypeKind.CXType_FunctionProto

        return conf.lib.clang_isFunctionTypeVariadic(self)

    def get_address_space(self):
        return conf.lib.clang_getAddressSpace(self)

    def get_typedef_name(self):
        return conf.lib.clang_getTypedefName(self)

    def is_pod(self):
        """Determine whether this Type represents plain old data (POD)."""
        return conf.lib.clang_isPODType(self)

    def get_pointee(self):
        """
        For pointer types, returns the type of the pointee.
        """
        return conf.lib.clang_getPointeeType(self)

    def get_declaration(self):
        """
        Return the cursor for the declaration of the given type.
        """
        return conf.lib.clang_getTypeDeclaration(self)

    def get_result(self):
        """
        Retrieve the result type associated with a function type.
        """
        return conf.lib.clang_getResultType(self)

    def get_array_element_type(self):
        """
        Retrieve the type of the elements of the array type.
        """
        return conf.lib.clang_getArrayElementType(self)

    def get_array_size(self):
        """
        Retrieve the size of the constant array.
        """
        return conf.lib.clang_getArraySize(self)

    def get_class_type(self):
        """
        Retrieve the class type of the member pointer type.
        """
        return conf.lib.clang_Type_getClassType(self)

    def get_named_type(self):
        """
        Retrieve the type named by the qualified-id.
        """
        return conf.lib.clang_Type_getNamedType(self)

    def get_align(self):
        """
        Retrieve the alignment of the record.
        """
        return conf.lib.clang_Type_getAlignOf(self)

    def get_size(self):
        """
        Retrieve the size of the record.
        """
        return conf.lib.clang_Type_getSizeOf(self)

    def get_offset(self, fieldname):
        """
        Retrieve the offset of a field in the record.
        """
        return conf.lib.clang_Type_getOffsetOf(self, fieldname)

    def get_ref_qualifier(self):
        """
        Retrieve the ref-qualifier of the type.
        """
        return conf.lib.clang_Type_getCXXRefQualifier(self)

    def get_fields(self):
        """Return an iterator for accessing the fields of this type."""

        fields = []

        def visitor(field, data):
            assert field != conf.lib.clang_getNullCursor()

            # Create reference to TU so it isn't GC'd before Cursor.
            field._tu = self._tu
            fields.append(field)
            return _C.CXVisitorResult.CXVisit_Continue

        conf.lib.clang_Type_visitFields(
            self, visitor, _C.voidp(0)
        )
        return iter(fields)

    def get_exception_specification_kind(self):
        """
        Return the kind of the exception specification; a value from
        the ExceptionSpecificationKind enumeration.
        """
        return conf.lib.clang.getExceptionSpecificationType(self)

    @property
    def spelling(self):
        """Retrieve the spelling of this Type."""
        return conf.lib.clang_getTypeSpelling(self)

    def __eq__(self, other):
        if type(other) != type(self):
            return False

        return conf.lib.clang_equalTypes(self, other)

    def __ne__(self, other):
        return not self.__eq__(other)


## CIndex Objects ##

# CIndex objects (derived from ClangObject) are essentially lightweight
# wrappers attached to some underlying object, which is exposed via CIndex as
# a void*.

@_enhance(_C.CXUnsavedFile)
class _CXUnsavedFile:
    """Helper for passing unsaved file arguments."""


class CompletionChunk(object):

    def __init__(self, completionString, key):
        self.cs = completionString
        self.key = key

    def __repr__(self):
        return "{'" + self.spelling + "', " + str(self.kind) + "}"

    @CachedProperty
    def spelling(self):
        return conf.lib.clang_getCompletionChunkText(self.cs, self.key)

    @CachedProperty
    def kind(self):
        return conf.lib.clang_getCompletionChunkKind(self.cs, self.key)

    @CachedProperty
    def string(self):
        res = conf.lib.clang_getCompletionChunkCompletionString(self.cs, self.key)

        if res:
            return CompletionString(res)
        else:
            None


class CompletionString(ClangObject):
    class Availability(object):
        def __init__(self, name):
            self.name = name

        def __str__(self):
            return self.name

        def __repr__(self):
            return "<Availability: %s>" % self

    def __len__(self):
        return self.num_chunks

    @CachedProperty
    def num_chunks(self):
        return conf.lib.clang_getNumCompletionChunks(self)

    def __getitem__(self, key):
        if self.num_chunks <= key:
            raise IndexError
        return CompletionChunk(self, key)

    @property
    def priority(self):
        return conf.lib.clang_getCompletionPriority(self)

    @property
    def availability(self):
        return conf.lib.clang_getCompletionAvailability(self)

    @property
    def briefComment(self):
        if hasattr(conf.lib, "clang_getCompletionBriefComment"):
            return conf.lib.clang_getCompletionBriefComment(self)
        return _CXString()

    def __repr__(self):
        return (
                " | ".join([str(a) for a in self])
                + " || Priority: "
                + str(self.priority)
                + " || Availability: "
                + str(self.availability)
                + " || Brief comment: "
                + str(self.briefComment)
        )


@_enhance(_C.CXCompletionResult)
class CodeCompletionResult:

    def __repr__(self):
        return str(CompletionString(self.get_completion_string()))

    @property
    def kind(self):
        return self.CursorKind

    @property
    def string(self):
        return CompletionString(self.get_completion_string())


@_enhance(_C.CXCodeCompleteResults)
class CCRStructure:

    def __len__(self):
        return self.NumResults

    def __getitem__(self, key):
        if len(self) <= key:
            raise IndexError

        return self.at(key)


class CodeCompletionResults(ClangObject):

    def __del__(self):
        conf.lib.clang_disposeCodeCompleteResults(self)

    @property
    def results(self):
        return self.ptr.contents

    @property
    def diagnostics(self):
        class DiagnosticsItr(object):
            def __init__(self, ccr):
                self.ccr = ccr

            def __len__(self):
                return int(conf.lib.clang_codeCompleteGetNumDiagnostics(self.ccr))

            def __getitem__(self, key):
                return conf.lib.clang_codeCompleteGetDiagnostic(self.ccr, key)

        return DiagnosticsItr(self)


class Index(ClangObject):
    """
    The Index type provides the primary interface to the Clang CIndex library,
    primarily by providing an interface for reading and parsing translation
    units.
    """

    @staticmethod
    def create(excludeDecls=False):
        """
        Create a new Index.
        Parameters:
        excludeDecls -- Exclude local declarations from translation units.
        """
        return Index(conf.lib.clang_createIndex(excludeDecls, 0))

    def __del__(self):
        conf.lib.clang_disposeIndex(self)

    def read(self, path):
        """Load a TranslationUnit from the given AST file."""
        return TranslationUnit.from_ast_file(path, self)

    def parse(self, path, args=None, unsaved_files=None, options=None):
        """Load the translation unit from the given source code file by running
        clang and generating the AST before loading. Additional command line
        parameters can be passed to clang via the args parameter.

        In-memory contents for files can be provided by passing a list of pairs
        to as unsaved_files, the first item should be the filenames to be mapped
        and the second should be the contents to be substituted for the
        file. The contents may be passed as strings or file objects.

        If an error was encountered during parsing, a TranslationUnitLoadError
        will be raised.
        """
        return TranslationUnit.from_source(path, args, unsaved_files, options, self)


class TranslationUnit(_C.CXTranslationUnitImplp):
    """Represents a source code translation unit.

    This is one of the main types in the API. Any time you wish to interact
    with Clang's representation of a source file, you typically start with a
    translation unit.
    """

    # Default parsing mode.
    PARSE_NONE = 0

    # Instruct the parser to create a detailed processing record containing
    # metadata not normally retained.
    PARSE_DETAILED_PROCESSING_RECORD = 1

    # Indicates that the translation unit is incomplete. This is typically used
    # when parsing headers.
    PARSE_INCOMPLETE = 2

    # Instruct the parser to create a pre-compiled preamble for the translation
    # unit. This caches the preamble (included files at top of source file).
    # This is useful if the translation unit will be reparsed and you don't
    # want to incur the overhead of reparsing the preamble.
    PARSE_PRECOMPILED_PREAMBLE = 4

    # Cache code completion information on parse. This adds time to parsing but
    # speeds up code completion.
    PARSE_CACHE_COMPLETION_RESULTS = 8

    # Flags with values 16 and 32 are deprecated and intentionally omitted.

    # Do not parse function bodies. This is useful if you only care about
    # searching for declarations/definitions.
    PARSE_SKIP_FUNCTION_BODIES = 64

    # Used to indicate that brief documentation comments should be included
    # into the set of code completions returned from this translation unit.
    PARSE_INCLUDE_BRIEF_COMMENTS_IN_CODE_COMPLETION = 128

    @staticmethod
    def _to_cx_unsaved_file(unsaved_files):
        unsaved_array = []
        for i, (name, contents) in enumerate(unsaved_files):
            if hasattr(contents, "read"):
                contents = contents.read()
            f = _CXUnsavedFile()
            name = _C.StringHolder(fspath(name))
            content = _C.StringHolder(contents)
            f.set_file_name(name)
            f._name = name  # keep alive
            f.set_contents(content)
            f._content = content  # keep alive
            f.Length = len(contents)
            unsaved_array.append(f)
        return unsaved_array

    @classmethod
    def from_source(
            cls, filename, args=None, unsaved_files=None, options=None, index=None
    ):
        """Create a TranslationUnit by parsing source.

        This is capable of processing source code both from files on the
        filesystem as well as in-memory contents.

        Command-line arguments that would be passed to clang are specified as
        a list via args. These can be used to specify include paths, warnings,
        etc. e.g. ["-Wall", "-I/path/to/include"].

        In-memory file content can be provided via unsaved_files. This is an
        iterable of 2-tuples. The first element is the filename (str or
        PathLike). The second element defines the content. Content can be
        provided as str source code or as file objects (anything with a read()
        method). If a file object is being used, content will be read until EOF
        and the read cursor will not be reset to its original position.

        options is a bitwise or of TranslationUnit.PARSE_XXX flags which will
        control parsing behavior.

        index is an Index instance to utilize. If not provided, a new Index
        will be created for this TranslationUnit.

        To parse source from the filesystem, the filename of the file to parse
        is specified by the filename argument. Or, filename could be None and
        the args list would contain the filename(s) to parse.

        To parse source from an in-memory buffer, set filename to the virtual
        filename you wish to associate with this source (e.g. "test.c"). The
        contents of that file are then provided in unsaved_files.

        If an error occurs, a TranslationUnitLoadError is raised.

        Please note that a TranslationUnit with parser errors may be returned.
        It is the caller's responsibility to check tu.diagnostics for errors.

        Also note that Clang infers the source language from the extension of
        the input filename. If you pass in source code containing a C++ class
        declaration with the filename "test.c" parsing will fail.
        """

        if index is None:
            index = Index.create()

        unsaved_array = []
        if unsaved_files is not None:
            unsaved_array = cls._to_cx_unsaved_file(unsaved_files)

        if options is None:
            options = _C.clang_defaultEditingTranslationUnitOptions()

        ptr = conf.lib.clang_parseTranslationUnit(
            index,
            fspath(filename) if filename is not None else None,
            args,
            unsaved_array,
            options,
        )

        if not ptr:
            raise TranslationUnitLoadError("Error parsing translation unit.")

        return cls(ptr, index=index)

    @classmethod
    def from_ast_file(cls, filename, index=None):
        """Create a TranslationUnit instance from a saved AST file.

        A previously-saved AST file (provided with -emit-ast or
        TranslationUnit.save()) is loaded from the filename specified.

        If the file cannot be loaded, a TranslationUnitLoadError will be
        raised.

        index is optional and is the Index instance to use. If not provided,
        a default Index will be created.

        filename can be str or PathLike.
        """
        if index is None:
            index = Index.create()

        ptr = conf.lib.clang_createTranslationUnit(index, fspath(filename))
        if not ptr:
            raise TranslationUnitLoadError(filename)

        return cls(ptr=ptr, index=index)

    def __init__(self, ptr, index):
        """Create a TranslationUnit instance.

        TranslationUnits should be created using one of the from_* @classmethod
        functions above. __init__ is only called internally.
        """
        assert isinstance(ptr, _C.CXTranslationUnitImplp)
        _C.CXTranslationUnitImplp.__init__(self, ptr.get_ptr())
        assert isinstance(index, Index)
        self.index = index

    def __del__(self):
        conf.lib.clang_disposeTranslationUnit(self)

    @property
    def cursor(self):
        """Retrieve the cursor that represents the given translation unit."""
        return conf.lib.clang_getTranslationUnitCursor(self)

    @property
    @functools.cache
    def spelling(self):
        """Get the original translation unit source file name."""
        return conf.lib.clang_getTranslationUnitSpelling(self)

    def get_includes(self):
        """
        Return an iterable sequence of FileInclusion objects that describe the
        sequence of inclusions in a translation unit. The first object in
        this sequence is always the input file. Note that this method will not
        recursively iterate over header files included through precompiled
        headers.
        """
        includes = []

        def visitor(fobj, loc, depth, data):
            if depth > 0:
                includes.append(FileInclusion(loc.file, File(fobj), loc, depth))

        # Automatically adapt CIndex/ctype pointers to python objects

        conf.lib.clang_getInclusions(
            self, visitor, _C.voidp(0)
        )

        return iter(includes)

    def get_file(self, filename):
        """Obtain a File from this translation unit."""

        return File.from_name(self, filename)

    def get_location(self, filename, position):
        """Obtain a SourceLocation for a file in this translation unit.

        The position can be specified by passing:

          - Integer file offset. Initial file offset is 0.
          - 2-tuple of (line number, column number). Initial file position is
            (0, 0)
        """
        f = self.get_file(filename)

        if isinstance(position, int):
            return SourceLocation.from_offset(self, f, position)

        return SourceLocation.from_position(self, f, position[0], position[1])

    def get_extent(self, filename, locations):
        """Obtain a SourceRange from this translation unit.

        The bounds of the SourceRange must ultimately be defined by a start and
        end SourceLocation. For the locations argument, you can pass:

          - 2 SourceLocation instances in a 2-tuple or list.
          - 2 int file offsets via a 2-tuple or list.
          - 2 2-tuple or lists of (line, column) pairs in a 2-tuple or list.

        e.g.

        get_extent('foo.c', (5, 10))
        get_extent('foo.c', ((1, 1), (1, 15)))
        """
        f = self.get_file(filename)

        if len(locations) < 2:
            raise Exception("Must pass object with at least 2 elements")

        start_location, end_location = locations

        if hasattr(start_location, "__len__"):
            start_location = SourceLocation.from_position(
                self, f, start_location[0], start_location[1]
            )
        elif isinstance(start_location, int):
            start_location = SourceLocation.from_offset(self, f, start_location)

        if hasattr(end_location, "__len__"):
            end_location = SourceLocation.from_position(
                self, f, end_location[0], end_location[1]
            )
        elif isinstance(end_location, int):
            end_location = SourceLocation.from_offset(self, f, end_location)

        assert isinstance(start_location, SourceLocation)
        assert isinstance(end_location, SourceLocation)

        return SourceRange.from_locations(start_location, end_location)

    @property
    def diagnostics(self):
        """
        Return an iterable (and indexable) object containing the diagnostics.
        """

        class DiagIterator(object):
            def __init__(self, tu):
                self.tu = tu

            def __len__(self):
                return int(conf.lib.clang_getNumDiagnostics(self.tu))

            def __getitem__(self, key):
                diag = conf.lib.clang_getDiagnostic(self.tu, key)
                if not diag:
                    raise IndexError
                return Diagnostic(diag)

        return DiagIterator(self)

    def reparse(self, unsaved_files=None, options=None):
        """
        Reparse an already parsed translation unit.

        In-memory contents for files can be provided by passing a list of pairs
        as unsaved_files, the first items should be the filenames to be mapped
        and the second should be the contents to be substituted for the
        file. The contents may be passed as strings or file objects.
        """
        unsaved_files_array = []
        if unsaved_files is not None:
            unsaved_files_array = self._to_cx_unsaved_file(unsaved_files)

        if options is None:
            options = _C.clang_defaultReparseOptions(self)

        err = conf.lib.clang_reparseTranslationUnit(
            self, len(unsaved_files), unsaved_files_array[0], options
        )
        assert err == 0

    def save(self, filename):
        """Saves the TranslationUnit to a file.

        This is equivalent to passing -emit-ast to the clang frontend. The
        saved file can be loaded back into a TranslationUnit. Or, if it
        corresponds to a header, it can be used as a pre-compiled header file.

        If an error occurs while saving, a TranslationUnitSaveError is raised.
        If the error was TranslationUnitSaveError.ERROR_INVALID_TU, this means
        the constructed TranslationUnit was not valid at time of save. In this
        case, the reason(s) why should be available via
        TranslationUnit.diagnostics().

        filename -- The path to save the translation unit to (str or PathLike).
        """
        options = conf.lib.clang_defaultSaveOptions(self)
        result = int(
            conf.lib.clang_saveTranslationUnit(self, fspath(filename), options)
        )
        if result != 0:
            raise TranslationUnitSaveError(result, "Error saving TranslationUnit.")

    def codeComplete(
            self,
            path,
            line,
            column,
            unsaved_files=None,
            include_macros=False,
            include_code_patterns=False,
            include_brief_comments=False,
    ):
        """
        Code complete in this translation unit.

        In-memory contents for files can be provided by passing a list of pairs
        as unsaved_files, the first items should be the filenames to be mapped
        and the second should be the contents to be substituted for the
        file. The contents may be passed as strings or file objects.
        """
        options = 0

        if include_macros:
            options += 1

        if include_code_patterns:
            options += 2

        if include_brief_comments:
            options += 4

        unsaved_files_array = []
        if unsaved_files is not None:
            unsaved_files_array = self._to_cx_unsaved_file(unsaved_files)

        ptr = conf.lib.clang_codeCompleteAt(
            self,
            fspath(path),
            line,
            column,
            unsaved_files_array,
            len(unsaved_files),
            options,
        )
        if ptr:
            return CodeCompletionResults(ptr)
        return None

    def get_tokens(self, locations=None, extent=None):
        """Obtain tokens in this translation unit.

        This is a generator for Token instances. The caller specifies a range
        of source code to obtain tokens for. The range can be specified as a
        2-tuple of SourceLocation or as a SourceRange. If both are defined,
        behavior is undefined.
        """
        if locations is not None:
            extent = SourceRange(start=locations[0], end=locations[1])

        return TokenGroup.get_tokens(self, extent)


class File(ClangObject):
    """
    The File class represents a particular source file that is part of a
    translation unit.
    """

    @staticmethod
    def from_name(translation_unit, file_name):
        """Retrieve a file handle within the given translation unit."""
        return File(conf.lib.clang_getFile(translation_unit, fspath(file_name)))

    @property
    def name(self):
        """Return the complete file and path name of the file."""
        return conf.lib.clang_getFileName(self)

    @property
    def time(self):
        """Return the last modification time of the file."""
        return conf.lib.clang_getFileTime(self)

    def __str__(self):
        return self.name

    def __repr__(self):
        return "<File: %s>" % (self.name)

    @staticmethod
    def from_result(res, fn, args):
        res = File(res)

        # Copy a reference to the TranslationUnit to prevent premature GC.
        res._tu = args[0]._tu
        return res


class FileInclusion(object):
    """
    The FileInclusion class represents the inclusion of one source file by
    another via a '#include' directive or as the input file for the translation
    unit. This class provides information about the included file, the including
    file, the location of the '#include' directive and the depth of the included
    file in the stack. Note that the input file has depth 0.
    """

    def __init__(self, src, tgt, loc, depth):
        self.source = src
        self.include = tgt
        self.location = loc
        self.depth = depth

    @property
    def is_input_file(self):
        """True if the included file is the input file."""
        return self.depth == 0


class CompilationDatabaseError(Exception):
    """Represents an error that occurred when working with a CompilationDatabase

    Each error is associated to an enumerated value, accessible under
    e.cdb_error. Consumers can compare the value with one of the ERROR_
    constants in this class.
    """

    # An unknown error occurred
    ERROR_UNKNOWN = 0

    # The database could not be loaded
    ERROR_CANNOTLOADDATABASE = 1

    def __init__(self, enumeration, message):
        assert isinstance(enumeration, int)

        if enumeration > 1:
            raise Exception(
                "Encountered undefined CompilationDatabase error "
                "constant: %d. Please file a bug to have this "
                "value supported." % enumeration
            )

        self.cdb_error = enumeration
        Exception.__init__(self, "Error %d: %s" % (enumeration, message))


class CompileCommand(object):
    """Represents the compile command used to build a file"""

    def __init__(self, cmd, ccmds):
        self.cmd = cmd
        # Keep a reference to the originating CompileCommands
        # to prevent garbage collection
        self.ccmds = ccmds

    @property
    def directory(self):
        """Get the working directory for this CompileCommand"""
        return conf.lib.clang_CompileCommand_getDirectory(self.cmd)

    @property
    def filename(self):
        """Get the working filename for this CompileCommand"""
        return conf.lib.clang_CompileCommand_getFilename(self.cmd)

    @property
    def arguments(self):
        """
        Get an iterable object providing each argument in the
        command line for the compiler invocation as a _CXString.

        Invariant : the first argument is the compiler executable
        """
        length = conf.lib.clang_CompileCommand_getNumArgs(self.cmd)
        for i in range(length):
            yield conf.lib.clang_CompileCommand_getArg(self.cmd, i)


class CompileCommands(object):
    """
    CompileCommands is an iterable object containing all CompileCommand
    that can be used for building a specific file.
    """

    def __init__(self, ccmds):
        self.ccmds = ccmds

    def __del__(self):
        conf.lib.clang_CompileCommands_dispose(self.ccmds)

    def __len__(self):
        return int(conf.lib.clang_CompileCommands_getSize(self.ccmds))

    def __getitem__(self, i):
        cc = conf.lib.clang_CompileCommands_getCommand(self.ccmds, i)
        if not cc:
            raise IndexError
        return CompileCommand(cc, self)

    @staticmethod
    def from_result(res, fn, args):
        if not res:
            return None
        return CompileCommands(res)


class CompilationDatabase(ClangObject):
    """
    The CompilationDatabase is a wrapper class around
    clang::tooling::CompilationDatabase

    It enables querying how a specific source file can be built.
    """

    def __del__(self):
        conf.lib.clang_CompilationDatabase_dispose(self)

    @staticmethod
    def from_result(res, fn, args):
        if not res:
            raise CompilationDatabaseError(0, "CompilationDatabase loading failed")
        return CompilationDatabase(res)

    @staticmethod
    def fromDirectory(buildDir):
        """Builds a CompilationDatabase from the database found in buildDir"""
        try:
            cdb, errorCode = conf.lib.clang_CompilationDatabase_fromDirectory(fspath(buildDir))
            cdb = CompilationDatabase(cdb)
        except CompilationDatabaseError as e:
            raise CompilationDatabaseError(
                errorCode, "CompilationDatabase loading failed"
            )
        return cdb

    def getCompileCommands(self, filename):
        """
        Get an iterable object providing all the CompileCommands available to
        build filename. Returns None if filename is not found in the database.
        """
        return conf.lib.clang_CompilationDatabase_getCompileCommands(
            self, fspath(filename)
        )

    def getAllCompileCommands(self):
        """
        Get an iterable object providing all the CompileCommands available from
        the database.
        """
        return conf.lib.clang_CompilationDatabase_getAllCompileCommands(self)


@_enhance(_C.CXToken)
class Token:
    """Represents a single token from the preprocessor.

    Tokens are effectively segments of source code. Source code is first parsed
    into tokens before being converted into the AST and Cursors.

    Tokens are obtained from parsed TranslationUnit instances. You currently
    can't create tokens manually.
    """

    @property
    def spelling(self):
        """The spelling of this token.

        This is the textual representation of the token in source.
        """
        return conf.lib.clang_getTokenSpelling(self._tu, self)

    @property
    def kind(self):
        """Obtain the TokenKind of the current token."""
        return conf.lib.clang_getTokenKind(self)

    @property
    def location(self):
        """The SourceLocation this Token occurs at."""
        return conf.lib.clang_getTokenLocation(self._tu, self)

    @property
    def extent(self):
        """The SourceRange this Token occupies."""
        return conf.lib.clang_getTokenExtent(self._tu, self)

    @property
    def cursor(self):
        """The Cursor this Token corresponds to."""
        cursor = Cursor()
        cursor._tu = self._tu

        conf.lib.clang_annotateTokens(self._tu, self, 1, cursor)

        return cursor


__force_wrap_return_map = [
    ("clang_CompilationDatabase_getAllCompileCommands", CompileCommands),
    ("clang_CompilationDatabase_getCompileCommands", CompileCommands),
    ("clang_CompileCommand_getArg", _CXString),
    ("clang_CompileCommand_getDirectory", _CXString),
    ("clang_CompileCommand_getFilename", _CXString),
    ("clang_formatDiagnostic", _CXString),
    ("clang_getArgType", Type),
    ("clang_getArrayElementType", Type),
    ("clang_getCanonicalType", Type),
    ("clang_getCompletionBriefComment", _CXString),
    ("clang_getCompletionChunkText", _CXString),
    ("clang_getCursorDefinition", Cursor),
    ("clang_getCursorDisplayName", _CXString),
    ("clang_getCursorReferenced", Cursor),
    ("clang_getCursorResultType", Type),
    ("clang_getCursorSpelling", _CXString),
    ("clang_getCursorType", Type),
    ("clang_getCursorUSR", _CXString),
    ("clang_Cursor_getMangling", _CXString),
    ("clang_getDeclObjCTypeEncoding", _CXString),
    ("clang_getDiagnosticCategoryText", _CXString),
    ("clang_getDiagnosticFixIt", _CXString),
    ("clang_getDiagnosticOption", _CXString),
    ("clang_getDiagnosticSpelling", _CXString),
    ("clang_getElementType", Type),
    ("clang_getEnumDeclIntegerType", Type),
    ("clang_getFileName", _CXString),
    ("clang_getIBOutletCollectionType", Type),
    ("clang_getIncludedFile", File),
    ("clang_getPointeeType", Type),
    ("clang_getResultType", Type),
    ("clang_getTokenSpelling", _CXString),
    ("clang_getTranslationUnitCursor", Cursor),
    ("clang_getTranslationUnitSpelling", _CXString),
    ("clang_getTypeDeclaration", Cursor),
    ("clang_getTypedefDeclUnderlyingType", Type),
    ("clang_getTypedefName", _CXString),
    ("clang_getTypeKindSpelling", _CXString),
    ("clang_getTypeSpelling", _CXString),
    ("clang_Cursor_getArgument", Cursor),
    ("clang_Cursor_getTemplateArgumentType", Type),
    ("clang_Cursor_getBriefCommentText", _CXString),
    ("clang_Cursor_getRawCommentText", _CXString),
    ("clang_Type_getClassType", Type),
    ("clang_Type_getTemplateArgumentAsType", Type),
    ("clang_Type_getNamedType", Type),
]


def __force_wrap_return():
    for name, ret_type in __force_wrap_return_map:
        raw_fn = getattr(_C, name)
        if hasattr(ret_type, "from_result"):
            setattr(_C, name, _result_as(ret_type)(raw_fn))


__force_wrap_return()
__all__ = [
    "AvailabilityKind",
    "CodeCompletionResults",
    "CompilationDatabase",
    "CompileCommands",
    "CompileCommand",
    "CursorKind",
    "Cursor",
    "Diagnostic",
    "File",
    "FixIt",
    "Index",
    "LinkageKind",
    "SourceLocation",
    "SourceRange",
    "TLSKind",
    "TokenKind",
    "Token",
    "TranslationUnitLoadError",
    "TranslationUnit",
    "TypeKind",
    "Type",
]
