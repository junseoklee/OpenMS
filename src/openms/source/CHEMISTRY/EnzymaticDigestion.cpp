// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2015.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Chris Bielow $
// $Authors: Marc Sturm, Chris Bielow $
// --------------------------------------------------------------------------

#include <OpenMS/CHEMISTRY/EnzymaticDigestion.h>
#include <OpenMS/CHEMISTRY/EnzymesDB.h>
#include <OpenMS/FORMAT/TextFile.h>
#include <OpenMS/SYSTEM/File.h>
#include <OpenMS/CONCEPT/LogStream.h>
#include <boost/regex.hpp>

#include <iostream>

using namespace std;

namespace OpenMS
{
  const std::string EnzymaticDigestion::NamesOfSpecificity[] = {"full", "semi", "none"};


  EnzymaticDigestion::EnzymaticDigestion() :
    missed_cleavages_(0),
    enzyme_(*EnzymesDB::getInstance()->getEnzyme("Trypsin")),
    specificity_(SPEC_FULL),
    use_log_model_(false),
    log_model_threshold_(0.25),
    model_data_()
  {
    // load the cleavage model from disk (might throw exceptions)
    TextFile tf;
    tf.load(File::find("./CHEMISTRY/MissedCleavage.model"), true);
    for (TextFile::ConstIterator it = tf.begin(); it != tf.end(); ++it)
    {
      String tmp = *it;
      if (tmp.trim().hasPrefix("#")) continue; // skip comments
      StringList components;
      tmp.split(' ', components);
      if (components.size() != 4)
      {
        throw Exception::ParseError(__FILE__, __LINE__, __PRETTY_FUNCTION__, String("split(' ',") + tmp + ")", String("Got ") + components.size() + " columns, expected 4!");
      }
      BindingSite bs(components[0].toInt(), components[1].trim());
      CleavageModel cl(components[2].toDouble(), components[3].toDouble());
      model_data_[bs] = cl;
    }
  }

  EnzymaticDigestion::EnzymaticDigestion(const EnzymaticDigestion& rhs) :
    missed_cleavages_(rhs.missed_cleavages_),
    enzyme_(rhs.enzyme_),
    specificity_(rhs.specificity_),
    use_log_model_(rhs.use_log_model_),
    log_model_threshold_(rhs.log_model_threshold_),
    model_data_(rhs.model_data_)
  {

  }

  /// Assignment operator
  EnzymaticDigestion& EnzymaticDigestion::operator=(const EnzymaticDigestion& rhs)
  {
    if (this != &rhs)
    {
      missed_cleavages_ = rhs.missed_cleavages_;
      enzyme_ = rhs.enzyme_;
      specificity_ = rhs.specificity_;
      use_log_model_ = rhs.use_log_model_;
      log_model_threshold_ = rhs.log_model_threshold_;
      model_data_ = rhs.model_data_;
    }
    return *this;
  }

  SignedSize EnzymaticDigestion::getMissedCleavages() const
  {
    return missed_cleavages_;
  }

  void EnzymaticDigestion::setMissedCleavages(SignedSize missed_cleavages)
  {
    missed_cleavages_ = missed_cleavages;
  }

  void EnzymaticDigestion::setEnzyme(const String enzyme_name)
  {
    enzyme_ = *EnzymesDB::getInstance()->getEnzyme(enzyme_name);
  }

  String EnzymaticDigestion::getEnzymeName() const
  {
    return enzyme_.getName();
  }

  EnzymaticDigestion::Specificity EnzymaticDigestion::getSpecificityByName(const String& name)
  {
    for (Size i = 0; i < SIZE_OF_SPECIFICITY; ++i)
    {
      if (name == NamesOfSpecificity[i]) return Specificity(i);
    }
    return SIZE_OF_SPECIFICITY;
  }

  EnzymaticDigestion::Specificity EnzymaticDigestion::getSpecificity() const
  {
    return specificity_;
  }

  void EnzymaticDigestion::setSpecificity(Specificity spec)
  {
    specificity_ = spec;
  }

  bool EnzymaticDigestion::isLogModelEnabled() const
  {
    return use_log_model_;
  }

  void EnzymaticDigestion::setLogModelEnabled(bool enabled)
  {
    use_log_model_ = enabled;
  }

  double EnzymaticDigestion::getLogThreshold() const
  {
    return log_model_threshold_;
  }

  void EnzymaticDigestion::setLogThreshold(double threshold)
  {
    log_model_threshold_ = threshold;
  }

  std::vector<Size> EnzymaticDigestion::tokenise(const AASequence& protein) const
  {
    String s=protein.toUnmodifiedString();
    boost::regex re(enzyme_.getRegEx());
    boost::sregex_token_iterator i(s.begin(), s.end(), re, -1);
    boost::sregex_token_iterator j;
    vector<Size> positions;
    Size pos = -1;
    while(i != j)
    {
      AASequence pep = AASequence::fromString((String)(*i++));
      pos += pep.size();
      positions.push_back(pos);
    }
    return positions;
  }

  bool EnzymaticDigestion::isCleavageSite_(
    const AASequence& protein, const AASequence::ConstIterator& iterator, const std::vector<Size> positions) const
  {
    if (enzyme_.getRegEx() == "()") // no cleavage
    {
      return false;
    }
    else 
    {
      if (!use_log_model_)
      {
        // naive digestion
        if (iterator != protein.end()-1)
        {
          return (std::find(positions.begin(), positions.end(), iterator - protein.begin()) != positions.end());
        }
        else
        {
          return (enzyme_.getRegEx().hasSubstring(iterator->getOneLetterCode()) && *iterator != 'P');
        }
      }
      else
      {
        if (!enzyme_.getRegEx().hasSubstring("?!P"))
        {
          throw Exception::InvalidParameter(__FILE__, __LINE__, __PRETTY_FUNCTION__, String("EnzymaticDigestion: enzyme '") + enzyme_.getName() + " does not support logModel!");
        }
        if (!enzyme_.getRegEx().hasSubstring(iterator->getOneLetterCode()) || *iterator == 'P') // wait for R or K
        {
            return false;
        }
        SignedSize pos = distance(AASequence::ConstIterator(protein.begin()),
                                    iterator) - 4; // start position in sequence
        double score_cleave = 0, score_missed = 0;
        for (SignedSize i = 0; i < 9; ++i)
        {
          if ((pos + i >= 0) && (pos + i < (SignedSize)protein.size()))
          {
            BindingSite bs(i, protein[pos + i].getOneLetterCode());
            Map<BindingSite, CleavageModel>::const_iterator pos_it =
              model_data_.find(bs);
            if (pos_it != model_data_.end()) // no data for non-std. amino acids
            {
              score_cleave += pos_it->second.p_cleave;
              score_missed += pos_it->second.p_miss;
            }
          }
        }
        return score_missed - score_cleave > log_model_threshold_;
      }
    }
  }

  void EnzymaticDigestion::nextCleavageSite_(const AASequence& protein, AASequence::ConstIterator& iterator, const std::vector<Size> positions) const
  {
    while (iterator != protein.end())
    {
      if (isCleavageSite_(protein, iterator, positions))
      {
        ++iterator;
        return;
      }
      ++iterator;
    }
    return;
  }

  bool EnzymaticDigestion::isValidProduct(const AASequence& protein, Size pep_pos, Size pep_length)
  {
    if (pep_pos >= protein.size())
    {
      LOG_WARN << "Error: start of peptide is beyond end of protein!" << endl;
      return false;
    }
    else if (pep_pos + pep_length > protein.size())
    {
      LOG_WARN << "Error: end of peptide is beyond end of protein!" << endl;
      return false;
    }
    else if (pep_length == 0 || protein.size() == 0)
    {
      LOG_WARN << "Error: peptide or protein must not be empty!" << endl;
      return false;
    }

    if (specificity_ == SPEC_NONE)
    {
      return true; // we don't care about terminal ends
    }
    else
    { // either SPEC_SEMI or SPEC_FULL
      bool spec_c = false, spec_n = false;

      std::vector<Size> positions = tokenise(protein);
      // test each end
      if (pep_pos == 0 ||
          (pep_pos == 1 && protein.getResidue((Size)0).getOneLetterCode() == "M") ||
          isCleavageSite_(protein, protein.begin() + (pep_pos - 1), positions))
      {
        spec_n = true;
      }

      if (pep_pos + pep_length == protein.size() ||
          isCleavageSite_(protein, protein.begin() + (pep_pos + pep_length - 1), positions))
      {
        spec_c = true;
      }

      if (spec_n && spec_c)
      {
        return true; // if both are fine, its definitely valid
      }
      else if ((specificity_ == SPEC_SEMI) && (spec_n || spec_c))
      {
        return true; // one only for SEMI
      }
      else
      {
        return false;
      }
    }
  }

  Size EnzymaticDigestion::peptideCount(const AASequence& protein)
  {
    SignedSize count = 1;
    vector<Size> positions = tokenise(protein);
    AASequence::ConstIterator iterator = protein.begin();
    while (nextCleavageSite_(protein, iterator, positions), iterator != protein.end())
    {
      ++count;
    }
    if (use_log_model_) missed_cleavages_ = 0; // log model has missed cleavages built-in

    // missed cleavages
    Size sum = count;
    for (SignedSize i = 1; i < count; ++i)
    {
      if (i > missed_cleavages_) break;
      sum += count - i;
    }
    return sum;
  }

  void EnzymaticDigestion::digest(const AASequence& protein, vector<AASequence>& output) const
  {
    // initialization
    SignedSize count = 1;
    output.clear();

    SignedSize missed_cleavages = missed_cleavages_;

    if (use_log_model_)
      missed_cleavages = 0; // log model has missed cleavages build-in

    // missed cleavage iterators
    std::vector<AASequence::ConstIterator> mc_iterators;
    if (missed_cleavages != 0)
      mc_iterators.push_back(protein.begin());
    // naive cleavage sites
    vector<Size> positions = tokenise(protein);
    cout << positions.size() <<endl;
    AASequence::ConstIterator begin = protein.begin();
    AASequence::ConstIterator end = protein.begin();
    while (nextCleavageSite_(protein, end, positions), end != protein.end())
    {
      ++count;
      if (missed_cleavages != 0)
      {
        mc_iterators.push_back(end);
      }

      output.push_back(protein.getSubsequence(begin - protein.begin(), end - begin));
      begin = end;
    }
    output.push_back(protein.getSubsequence(begin - protein.begin(), end - begin));
    if (missed_cleavages != 0)
    {
      mc_iterators.push_back(end);
    }

    //missed cleavages
    if (mc_iterators.size() > 2) //there is at least one cleavage site!
    {
      //resize to number of fragments
      Size sum = count;

      for (SignedSize i = 1; i < count; ++i)
      {
        if (i > missed_cleavages_)
          break;
        sum += count - i;
      }
      output.resize(sum);

      //generate fragments with missed cleavages
      Size pos = count;
      for (SignedSize i = 1; ((i <= missed_cleavages_) && (count > i)); ++i)
      {
        vector<AASequence::ConstIterator>::const_iterator b = mc_iterators.begin();
        vector<AASequence::ConstIterator>::const_iterator e = b + (i + 1);
        while (e != mc_iterators.end())
        {
          output[pos] = AASequence(protein.getSubsequence(*b - protein.begin(), *e  - *b));
          ++b;
          ++e;
          ++pos;
        }
      }
    }
  }

} //namespace
