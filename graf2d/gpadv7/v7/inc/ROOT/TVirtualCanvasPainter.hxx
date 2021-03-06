/// \file ROOT/TVirtualCanvasPainter.hxx
/// \ingroup Gpad ROOT7
/// \author Axel Naumann <axel@cern.ch>
/// \date 2017-05-31
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2017, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT7_TVirtualCanvasPainter
#define ROOT7_TVirtualCanvasPainter

#include "ROOT/TPadPainter.hxx"

#include <memory>
#include <functional>

namespace ROOT {
namespace Experimental {

using CanvasCallback_t = std::function<void(bool)>;

class TCanvas;
class TWebWindow;

namespace Internal {

/** \class ROOT::Experimental::Internal::TVirtualCanvasPainter
  Abstract interface for painting a canvas.
  */

class TVirtualCanvasPainter : public TPadPainter {
protected:
   class Generator {
   public:
      /// Abstract interface to create a TVirtualCanvasPainter implementation.
      virtual std::unique_ptr<TVirtualCanvasPainter> Create(const TCanvas &canv) const = 0;
      /// Default destructor.
      virtual ~Generator();
   };

   /// generator getter
   static std::unique_ptr<Generator> &GetGenerator();

public:
   /// Default destructor.
   virtual ~TVirtualCanvasPainter();

   /// indicate that canvas changed, provides current version of the canvas
   virtual void CanvasUpdated(uint64_t, bool, CanvasCallback_t) = 0;

   /// return true if canvas modified since last painting
   virtual bool IsCanvasModified(uint64_t) const = 0;

   /// perform special action when drawing is ready
   virtual void DoWhenReady(const std::string &, const std::string &, bool, CanvasCallback_t) = 0;

   virtual void NewDisplay(const std::string &where) = 0;

   virtual bool AddPanel(std::shared_ptr<TWebWindow>) { return false; }

   /// Loads the plugin that implements this class.
   static std::unique_ptr<TVirtualCanvasPainter> Create(const TCanvas &canv);
};
} // namespace Internal
} // namespace Experimental
} // namespace ROOT

#endif // ROOT7_TVirtualCanvasPainter
