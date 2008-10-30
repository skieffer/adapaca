/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 1.3.36
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */

namespace org.adaptagrams.cola {

using System;
using System.Runtime.InteropServices;

public class ConstrainedFDLayout : IDisposable {
  private HandleRef swigCPtr;
  protected bool swigCMemOwn;

  internal ConstrainedFDLayout(IntPtr cPtr, bool cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = new HandleRef(this, cPtr);
  }

  internal static HandleRef getCPtr(ConstrainedFDLayout obj) {
    return (obj == null) ? new HandleRef(null, IntPtr.Zero) : obj.swigCPtr;
  }

  ~ConstrainedFDLayout() {
    Dispose();
  }

  public virtual void Dispose() {
    lock(this) {
      if(swigCPtr.Handle != IntPtr.Zero && swigCMemOwn) {
        swigCMemOwn = false;
        colaPINVOKE.delete_ConstrainedFDLayout(swigCPtr);
      }
      swigCPtr = new HandleRef(null, IntPtr.Zero);
      GC.SuppressFinalize(this);
    }
  }

  public ConstrainedFDLayout(RectPtrVector rs, EdgeVector es, double idealLength, SWIGTYPE_p_std__valarrayT_double_t eweights, TestConvergence done, PreIteration preIteration) : this(colaPINVOKE.new_ConstrainedFDLayout__SWIG_0(RectPtrVector.getCPtr(rs), EdgeVector.getCPtr(es), idealLength, SWIGTYPE_p_std__valarrayT_double_t.getCPtr(eweights), TestConvergence.getCPtr(done), PreIteration.getCPtr(preIteration)), true) {
    if (colaPINVOKE.SWIGPendingException.Pending) throw colaPINVOKE.SWIGPendingException.Retrieve();
  }

  public ConstrainedFDLayout(RectPtrVector rs, EdgeVector es, double idealLength, SWIGTYPE_p_std__valarrayT_double_t eweights, TestConvergence done) : this(colaPINVOKE.new_ConstrainedFDLayout__SWIG_1(RectPtrVector.getCPtr(rs), EdgeVector.getCPtr(es), idealLength, SWIGTYPE_p_std__valarrayT_double_t.getCPtr(eweights), TestConvergence.getCPtr(done)), true) {
    if (colaPINVOKE.SWIGPendingException.Pending) throw colaPINVOKE.SWIGPendingException.Retrieve();
  }

  public ConstrainedFDLayout(RectPtrVector rs, EdgeVector es, double idealLength, SWIGTYPE_p_std__valarrayT_double_t eweights) : this(colaPINVOKE.new_ConstrainedFDLayout__SWIG_2(RectPtrVector.getCPtr(rs), EdgeVector.getCPtr(es), idealLength, SWIGTYPE_p_std__valarrayT_double_t.getCPtr(eweights)), true) {
    if (colaPINVOKE.SWIGPendingException.Pending) throw colaPINVOKE.SWIGPendingException.Retrieve();
  }

  public ConstrainedFDLayout(RectPtrVector rs, EdgeVector es, double idealLength) : this(colaPINVOKE.new_ConstrainedFDLayout__SWIG_3(RectPtrVector.getCPtr(rs), EdgeVector.getCPtr(es), idealLength), true) {
    if (colaPINVOKE.SWIGPendingException.Pending) throw colaPINVOKE.SWIGPendingException.Retrieve();
  }

  public void run(bool x, bool y) {
    colaPINVOKE.ConstrainedFDLayout_run__SWIG_0(swigCPtr, x, y);
  }

  public void run(bool x) {
    colaPINVOKE.ConstrainedFDLayout_run__SWIG_1(swigCPtr, x);
  }

  public void run() {
    colaPINVOKE.ConstrainedFDLayout_run__SWIG_2(swigCPtr);
  }

  public void runOnce(bool x, bool y) {
    colaPINVOKE.ConstrainedFDLayout_runOnce__SWIG_0(swigCPtr, x, y);
  }

  public void runOnce(bool x) {
    colaPINVOKE.ConstrainedFDLayout_runOnce__SWIG_1(swigCPtr, x);
  }

  public void runOnce() {
    colaPINVOKE.ConstrainedFDLayout_runOnce__SWIG_2(swigCPtr);
  }

  public void setXConstraints(CompoundConstraintsVector ccsx) {
    colaPINVOKE.ConstrainedFDLayout_setXConstraints(swigCPtr, CompoundConstraintsVector.getCPtr(ccsx));
  }

  public void setYConstraints(CompoundConstraintsVector ccsy) {
    colaPINVOKE.ConstrainedFDLayout_setYConstraints(swigCPtr, CompoundConstraintsVector.getCPtr(ccsy));
  }

  public void setTopology(TopologyNodePtrVector tnodes, TopologyEdgePtrVector routes) {
    colaPINVOKE.ConstrainedFDLayout_setTopology(swigCPtr, TopologyNodePtrVector.getCPtr(tnodes), TopologyEdgePtrVector.getCPtr(routes));
  }

  public void setUnsatisfiableConstraintInfo(SWIGTYPE_p_UnsatisfiableConstraintInfos unsatisfiableX, SWIGTYPE_p_UnsatisfiableConstraintInfos unsatisfiableY) {
    colaPINVOKE.ConstrainedFDLayout_setUnsatisfiableConstraintInfo(swigCPtr, SWIGTYPE_p_UnsatisfiableConstraintInfos.getCPtr(unsatisfiableX), SWIGTYPE_p_UnsatisfiableConstraintInfos.getCPtr(unsatisfiableY));
  }

  public double computeStress() {
    double ret = colaPINVOKE.ConstrainedFDLayout_computeStress(swigCPtr);
    return ret;
  }

}

}