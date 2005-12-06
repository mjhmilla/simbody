/* Copyright (c) 2005-6 Stanford University and Michael Sherman.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including 
 * without limitation the rights to use, copy, modify, merge, publish, 
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included 
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**@file
 * Implementations of non-inline methods of FeatureRep.
 */

#include "SimbodyCommon.h"
#include "Feature.h"
#include "FeatureRep.h"

#include <string>
#include <iostream> 
#include <sstream>
using std::cout;
using std::endl;
using std::ostream;


// Returns -1, 0, 1 according to key {<,==,>} test ignoring case.
static int caseInsensitiveCompare(const std::string& key, const std::string& test);

namespace simtk {

    // FEATURE REP //

void FeatureRep::realize(/*State,*/Stage g) const {
    for (int i=0; i < getNSubfeatures(); ++i)
        getSubfeature(i).realize(g);

    if (hasPlacement()) 
        getPlacement().realize(/*State,*/ g);
}

std::string 
FeatureRep::getFullName() const { 
    std::string s;
    if (hasParentFeature())
        s = getParentFeature().getFullName() + "/";
    return s + getName(); 
}

const Feature& FeatureRep::findRootFeature() const {
    return hasParentFeature() 
        ? getParentFeature().getRep().findRootFeature() : getMyHandle();
}
Feature& FeatureRep::findUpdRootFeature() {
    return hasParentFeature() 
        ? updParentFeature().updRep().findUpdRootFeature() : updMyHandle();
}

// These are default implementations. Derived features which can actually
// be used as a placement of the given type should override.

/*virtual*/ PlacementRep*
FeatureRep::useFeatureAsRealPlacement(RealPlacement&) const {
    SIMTK_THROW3(Exception::FeatureCantBeUsedAsPlacement,
                    getFullName(), getFeatureTypeName(), "Real");
    //NOTREACHED
    return 0;
}
/*virtual*/ PlacementRep*
FeatureRep::useFeatureAsVec3Placement(Vec3Placement&) const {
    SIMTK_THROW3(Exception::FeatureCantBeUsedAsPlacement,
                    getFullName(), getFeatureTypeName(), "Vec3");
    //NOTREACHED
    return 0;
}
/*virtual*/ PlacementRep*
FeatureRep::useFeatureAsStationPlacement(StationPlacement&) const {
    SIMTK_THROW3(Exception::FeatureCantBeUsedAsPlacement,
                    getFullName(), getFeatureTypeName(), "Station");
    //NOTREACHED
    return 0;
}
/*virtual*/ PlacementRep*
FeatureRep::useFeatureAsDirectionPlacement(DirectionPlacement&) const {
    SIMTK_THROW3(Exception::FeatureCantBeUsedAsPlacement,
                    getFullName(), getFeatureTypeName(), "Direction");
    //NOTREACHED
    return 0;
}    
/*virtual*/ PlacementRep*
FeatureRep::useFeatureAsOrientationPlacement(OrientationPlacement&) const {
    SIMTK_THROW3(Exception::FeatureCantBeUsedAsPlacement,
                    getFullName(), getFeatureTypeName(), "Orientation");
    //NOTREACHED
    return 0;
} 
/*virtual*/ PlacementRep*
FeatureRep::useFeatureAsFramePlacement(FramePlacement&) const {
    SIMTK_THROW3(Exception::FeatureCantBeUsedAsPlacement,
                    getFullName(), getFeatureTypeName(), "Frame");
    //NOTREACHED
    return 0;
} 

void 
FeatureRep::cloneWithoutParentOrExternalPlacements(Feature& newHandle) const
{
    FeatureRep* copy = clone();
    copy->setMyHandle(newHandle);
    copy->parent = 0; copy->indexInParent = -1;
    newHandle.setRep(copy);

    // Re-parent all the copied child Features to their new parent,
    // and fix the owned Placements to acknowledge their new owner.
    copy->reparentMyChildren();

    // Fix up all the internal placement references and delete the
    // external ones.
    copy->fixPlacements(this->getMyHandle(), copy->getMyHandle());
}

// Use a Placement like p (possibly recast to something else) for this
// feature. Concrete FeatureRep's are responsible for interpreting the
// Placement and possibly converting it to something usable.
//
// We have to decide on an owner feature for the placement expression.
// That is the youngest common ancestor of this feature and all features
// mentioned explicitly in the placement expression.
//
// If this placement is currently evalutable (meaning it is a constant
// or references only features with evaluatable placements) then we
// can allocate a value slot for it in the owner feature. In addition,
// this may have enabled evaluation of any number of additional placements
// which were dependent (directly or indirectly) on the placement of 
// this feature. Value slots for a given placement expression x are always
// owned by the oldest owner of any of the placements on which x recursively
// depends.
void FeatureRep::place(const Placement& p) {
    assert(p.hasRep());

    // If possible, create a fixed-up copy of p which is suitable for
    // use as a Placement for this concrete FeatureRep.
    Placement pTweaked = 
        p.getRep().getPlacementType() == getRequiredPlacementType()
        ? p : convertToRequiredPlacementType(p);
    if (!pTweaked.hasRep()) {
        SIMTK_THROW3(Exception::PlacementCantBeUsedForThisFeature,
            PlacementRep::getPlacementTypeName(p.getRep().getPlacementType()),
            getFullName(), getFeatureTypeName());
        //NOTREACHED             
    }

    assert(pTweaked.getRep().getPlacementType() == getRequiredPlacementType());
    
    // If the Placement references any features, all its references
    // must be on the same feature tree as this feature (although not necessarily
    // *below* this feature). We will make the placement owner be the youngest
    // common ancestor of this feature and all the features referenced (directly)
    // by the placement. Note that this is not a recursive search through the
    // referenced features' placements -- we only care about direct feature 
    // references, not how they are placed (they may not even have placements
    // yet at all).

    const Feature* offender;
    if (!pTweaked.getRep().isLimitedToSubtree(findRootFeature(), offender)) {
        SIMTK_THROW2(Exception::FeatureAndPlacementOnDifferentTrees,
            getFullName(), offender->getFullName());
        //NOTREACHED
    }

    // If the Placement doesn't reference any features, it is a constant
    // value and can be owned by anyone. If the current Feature is a
    // prototype (has no parent) then we are "locking down" a value in
    // the prototype and the current Feature can own the placement itself.
    // If on the other hand the current Feature has a parent, then we
    // want the parent to own the placement (making it external). This is a 
    // significant difference because in the self-placement case the placement 
    // would remain in place after a copy, whereas external placements are
    // removed by copy (or assign). So either this Feature (if alone) or its
    // parent will be the youngest conceivable owner for the new Placement.
    const Feature& youngestAllowed = hasParentFeature() ? getParentFeature() : getMyHandle();

    const Feature* commonAncestor = 
        pTweaked.getRep().findAncestorFeature(youngestAllowed);
    assert(commonAncestor); // there has to be one since they are on the same tree!

    // Please look the other way for a moment while we make a small change to
    // this const Feature ...
    Placement& good = 
        const_cast<Feature*>(commonAncestor)->updRep().addPlacementLike(pTweaked);

    // Some sanity (insanity?) checks.
    assert(good.hasOwner());
    assert(good.isConstant() || !good.getOwner().isSameFeature(getMyHandle()));
    assert(FeatureRep::isFeatureInFeatureTree(good.getOwner(), getMyHandle()));
    assert(!good.dependsOn(getMyHandle())); // depends on *is* recursive
    placement = &good; 
    postProcessNewPlacement();
}

Feature& 
FeatureRep::addSubfeatureLike(const Feature& f, const std::string& nm) {
    assert(nm.size() > 0);
    const int index = (int)subfeatures.size();
    subfeatures.push_back(SubFeature()); // an empty handle
    Feature& newFeature = subfeatures[index];
    f.getRep().cloneWithoutParentOrExternalPlacements(newFeature);
    newFeature.updRep().setParentFeature(updMyHandle(), index);
    newFeature.updRep().setName(nm);
    postProcessNewSubfeature(newFeature);
    return newFeature;
}

// Note that we can only allow placements involving this feature, its children,
// grandchildren, etc. -- no external references. Otherwise someone further
// up the tree should own the new placement.
Placement& 
FeatureRep::addPlacementLike(const Placement& p) {
    assert(p.hasRep());

    const Feature* offender;
    if (!p.getRep().isLimitedToSubtree(getMyHandle(),offender)) {
        SIMTK_THROW3(Exception::PlacementMustBeLocal,"FeatureRep::addPlacementLike",
            this->getFullName(),offender->getFullName());
    }

    const int index = (int)placementExpressions.size();
    placementExpressions.push_back(SubPlacement());
    Placement& newPlacement = placementExpressions[index];
    p.getRep().cloneUnownedWithNewHandle(newPlacement);
    newPlacement.updRep().setOwner(getMyHandle(), index);
    return newPlacement;
}

PlacementValue& 
FeatureRep::addPlacementValueLike(const PlacementValue& v) {
    assert(v.hasRep());

    const int index = (int)placementValues.size();
    placementValues.push_back(PlacementValue());
    PlacementValue& newPlacementValue = placementValues[index];
    v.getRep().cloneUnownedWithNewHandle(newPlacementValue);
    newPlacementValue.updRep().setOwner(getMyHandle(), index);
    return newPlacementValue;
}

// Is Feature f in the tree rooted at oldRoot? If so, optionally return the 
// series of indices required to get to this Feature from the root.
// Complexity is O(log n) where n is tree depth.
/*static*/ bool 
FeatureRep::isFeatureInFeatureTree(const Feature& oldRoot, const Feature& f,
                                std::vector<int>* trace)
{
    if (trace) trace->clear();
    const Feature* const oldp = &oldRoot;
    const Feature*       fp   = &f;

    while (fp != oldp) {
        const Feature* const fpParent = getParentPtr(*fp);
        if (!fpParent) {
            if (trace) trace->clear(); // never mind ...
            return false;
        }
        if (trace) trace->push_back(fp->rep->getIndexInParent());
        fp = fpParent;
    }

    return true;
}

// Is Placement p owned by a Feature in the tree rooted at oldRoot?
/*static*/ bool 
FeatureRep::isPlacementInFeatureTree(const Feature& oldRoot, const Placement& p)
{
    if (!p.hasOwner())
        return false;   // a disembodied Placement
    return isFeatureInFeatureTree(oldRoot, p.getOwner());
}

// If Feature f is a member of the Feature tree rooted at oldRoot, find
// the corresponding Feature in the tree rooted at newRoot (which is expected
// to be a copy of oldRoot). Return NULL if not found for any reason.
/*static*/ const Feature* 
FeatureRep::findCorrespondingFeature
    (const Feature& oldRoot, const Feature& f, const Feature& newRoot)
{
    std::vector<int> trace;
    if (!isFeatureInFeatureTree(oldRoot,f,&trace))
        return 0;

    // Trace holds the indices needed to step from newRoot down to
    // the corresponding Feature (in reverse order).
    const Feature* newTreeRef = &newRoot;
    for (size_t i=trace.size(); i >=1; --i)
        newTreeRef = &newTreeRef->getRep().getSubfeature(trace[i-1]);
    return newTreeRef;
}

// Given two features, run up the tree towards the root to find
// their "least common denominator", i.e. the first shared node
// on the path back to the root. Return a pointer to that node
// if found, otherwise NULL meaning that the features aren't on
// the same tree. If the features are the same, then
// that feature is the answer.
// Complexity is O(log n) (3 passes) where n is depth of Feature tree.

/*static*/ const Feature* 
FeatureRep::findYoungestCommonAncestor(const Feature& f1, const Feature& f2)
{
    std::vector<const Feature*> f1path, f2path; // paths from nodes to their roots
    const Feature* f1p = &f1;
    const Feature* f2p = &f2;
    while (f1p) {f1path.push_back(f1p); f1p = getParentPtr(*f1p);}
    while (f2p) {f2path.push_back(f2p); f2p = getParentPtr(*f2p);}

    // If there is a common ancestor, we can find it by searching down from
    // the root (last element in each path). As soon as there is a difference,
    // the previous element is the common ancestor.
    const Feature* ancestor = 0;
    size_t i1 = f1path.size(), i2 = f2path.size();  // index of element just past end
    while (i1 && i2) {
        if (f1path[--i1] != f2path[--i2])
            break;
        ancestor = f1path[i1];
    }
    return ancestor;
}

/*static*/ Feature* 
FeatureRep::findUpdYoungestCommonAncestor(Feature& f1, const Feature& f2) {
    return const_cast<Feature*>(findYoungestCommonAncestor(f1,f2));
}

// Debugging routine
void FeatureRep::checkFeatureConsistency(const Feature* expParent, 
                                         int expIndexInParent,
                                         const Feature& root) const {
    cout << "CHECK FEATURE CONSISTENCY FOR FeatureRep@" << this << "(" << getFullName() << ")" << endl;

    if (!myHandle) 
        cout << "*** NO HANDLE ***" << endl;
    else if (myHandle->rep != this)
        cout << "*** Handle->rep=" << myHandle->rep << " which is *** WRONG ***" << endl;

    if (parent != expParent)
        cout << " WRONG PARENT@" << parent << "; should have been " << expParent << endl;
    if (indexInParent != expIndexInParent)
        cout << "*** WRONG INDEX " << indexInParent << "; should have been " << expIndexInParent << endl;

    if (!findRootFeature().isSameFeature(root)) {
        cout << " WRONG ROOT@" << &findRootFeature() << "(" << findRootFeature().getFullName() << ")";
        cout << "; should have been " << &root << "(" << root.getFullName() << ")" << endl;
    }
    for (size_t i=0; i<(size_t)getNSubfeatures(); ++i) 
        getSubfeature(i).checkFeatureConsistency(&getMyHandle(), (int)i, root);
    for (size_t i=0; i<(size_t)getNPlacementExpressions(); ++i) 
        getPlacementExpression(i).checkPlacementConsistency(&getMyHandle(), (int)i, root);
    for (size_t i=0; i < (size_t)getNPlacementValues(); ++i)
        getPlacementValue(i).checkPlacementValueConsistency(&getMyHandle(), (int)i, root);
}

// Return true and ix==feature index if a feature of the given name is found.
// Otherwise return false and ix==childFeatures.size().
bool 
FeatureRep::findSubfeatureIndex(const std::string& nm, size_t& ix) const {
    for (ix=0; ix < (size_t)getNSubfeatures(); ++ix)
        if (caseInsensitiveCompare(nm, subfeatures[ix].getName())==0)
            return true;
    return false;   // not found
}

// We have just copied a Feature subtree so all the parent pointers are
// wrong. Recursively repair them to point into the new tree.
void FeatureRep::reparentMyChildren() {
    for (size_t i=0; i < (size_t)getNSubfeatures(); ++i) {
        assert(subfeatures[i].getRep().hasParentFeature());
        assert(subfeatures[i].getRep().getIndexInParent() == i);    // shouldn't change
        subfeatures[i].updRep().setParentFeature(updMyHandle(), i);
        subfeatures[i].updRep().reparentMyChildren();               // recurse
    }
    for (size_t i=0; i < (size_t)getNPlacementExpressions(); ++i) {
        assert(placementExpressions[i].getRep().hasOwner());
        assert(placementExpressions[i].getRep().getIndexInOwner() == i);
        placementExpressions[i].updRep().setOwner(getMyHandle(), i);
    }
    for (size_t i=0; i < (size_t)getNPlacementValues(); ++i) {
        assert(placementValues[i].getRep().hasOwner());
        assert(placementValues[i].getRep().getIndexInOwner() == i);
        placementValues[i].updRep().setOwner(getMyHandle(), i);
    }
}

// We have just created at newRoot a copy of the tree rooted at oldRoot, and the
// current Feature (for which this is the Rep) is a node in the newRoot tree
// (with correct myHandle). However, the 'placement' pointers 
// still retain the values they had in the oldRoot tree; they must be 
// changed to point to the corresponding entities in the newRoot tree.
// If these pointers point outside the oldRoot tree, however, we'll just
// set them to 0 in the newRoot copy.
void FeatureRep::fixPlacements(const Feature& oldRoot, const Feature& newRoot) {
    for (size_t i=0; i < (size_t)getNSubfeatures(); ++i)
        subfeatures[i].updRep().fixPlacements(oldRoot, newRoot);    // recurse

    for (size_t i=0; i < (size_t)getNPlacementExpressions(); ++i) {
        PlacementRep& pr = placementExpressions[i].updRep();
        pr.repairFeatureReferences(oldRoot,newRoot);
        pr.repairValueReference(oldRoot,newRoot);
    }

    if (placement)
        placement = findCorrespondingPlacement(oldRoot,*placement,newRoot);
}

// If Placement p's owner Feature is a member of the Feature tree rooted at oldRoot,
// find the corresponding Placement in the tree rooted at newRoot (which is expected
// to be a copy of oldRoot). Return NULL if not found for any reason.
/*static*/ const Placement* 
FeatureRep::findCorrespondingPlacement
    (const Feature& oldRoot, const Placement& p, const Feature& newRoot)
{
    if (!p.hasOwner()) return 0;
    const Feature* corrOwner = findCorrespondingFeature(oldRoot,p.getOwner(),newRoot);
    if (!corrOwner) return 0;
    assert(corrOwner->hasRep());

    const Placement* newTreeRef = 
        &corrOwner->getRep().getPlacementExpression(p.getIndexInOwner());
    assert(newTreeRef);
    assert(&newTreeRef->getOwner() == corrOwner);
    assert(newTreeRef->getIndexInOwner() == p.getIndexInOwner());
    return newTreeRef;
}

// If PlacementValue v's owner Feature is a member of the Feature tree rooted at oldRoot,
// find the corresponding PlacementValue in the tree rooted at newRoot (which is expected
// to be a copy of oldRoot). Return NULL if not found for any reason.
/*static*/ const PlacementValue* 
FeatureRep::findCorrespondingPlacementValue
    (const Feature& oldRoot, const PlacementValue& v, const Feature& newRoot)
{
    if (!v.hasOwner()) return 0;
    const Feature* corrOwner = findCorrespondingFeature(oldRoot,v.getOwner(),newRoot);
    if (!corrOwner) return 0;
    assert(corrOwner->hasRep());

    const PlacementValue* newTreeRef = 
        &corrOwner->getRep().getPlacementValue(v.getIndexInOwner());
    assert(newTreeRef);
    assert(&newTreeRef->getOwner() == corrOwner);
    assert(newTreeRef->getIndexInOwner() == v.getIndexInOwner());
    return newTreeRef;
}

// For now we'll allow only letters, digits, and underscore in names. Case is retained
// for display but otherwise insignificant.
bool FeatureRep::isLegalFeatureName(const std::string& n) {
    if (n.size()==0) return false;
    for (size_t i=0; i<n.size(); ++i)
        if (!(isalnum(n[i]) || n[i]=='_'))
            return false;
    return true;
}

// Take pathname of the form xxx/yyy/zzz, check its validity and optionally
// return as a list of separate feature names. We return true if we're successful,
// false if the pathname is malformed in some way. In that case the last segment
// returned will be the one that caused trouble.
bool FeatureRep::isLegalFeaturePathname(const std::string& pathname, 
                                        std::vector<std::string>* segments)
{
    std::string t;
    const size_t end = pathname.size();
    size_t nxt = 0;
    if (segments) segments->clear();
    bool foundAtLeastOne = false;
    // for each segment
    while (nxt < end) {
        // for each character of a segment
        while (nxt < end && pathname[nxt] != '/')
            t += pathname[nxt++];
        foundAtLeastOne = true;
        if (segments) segments->push_back(t);
        if (!isLegalFeatureName(t))
            return false;
        t.clear();
        ++nxt; // skip '/' (or harmless extra increment at end)
    }
    return foundAtLeastOne;
}

} // namespace simtk



static int caseInsensitiveCompare(const std::string& key, const std::string& test) {
    const size_t minlen = std::min(key.size(), test.size());
    for (size_t i=0; i < minlen; ++i) {
        const int k = tolower(key[i]), t = tolower(test[i]);
        if (k < t) return -1;
        else if (k > t) return 1;
    }
    // caution -- size() is unsigned, don't get clever here
    if (key.size() > minlen) return 1;
    else if (test.size() > minlen) return -1;
    return 0;
}

