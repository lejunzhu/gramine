.. program:: gramine

==================================================================
:program:`gramine-direct`, :command:`gramine-sgx` -- Run something
==================================================================

.. note::

   This page is a stub.

Synopsis
========

| :command:`gramine-direct` {<*APPLICATION*>} [<*ARGS*> ...]
| :command:`gramine-sgx` {<*APPLICATION*>} [<*ARGS*> ...]

Description
===========

This is the main way to invoke Gramine. The first argument is the name of the
application (that is, name of the manifest file *without* ``.manifest``).

Environment variables
=====================

.. envvar:: GRAMINE_NO_AUTO_GET_TOKEN

   If not empty, :command:`gramine-sgx` will not automatically generate
   EINITTOKEN (for out-of-tree EPID driver), or its dummy counterpart (for
   upstream and DCAP drivers).
