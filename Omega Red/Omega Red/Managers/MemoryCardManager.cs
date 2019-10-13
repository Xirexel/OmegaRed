﻿/*  Omega Red - Client PS2 Emulator for PCs
*
*  Omega Red is free software: you can redistribute it and/or modify it under the terms
*  of the GNU Lesser General Public License as published by the Free Software Found-
*  ation, either version 3 of the License, or (at your option) any later version.
*
*  Omega Red is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
*  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
*  PURPOSE.  See the GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License along with Omega Red.
*  If not, see <http://www.gnu.org/licenses/>.
*/

using Omega_Red.Models;
using Omega_Red.Properties;
using Omega_Red.SocialNetworks.Google;
using Omega_Red.Tools;
using Omega_Red.Tools.Converters;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Data;
using System.Windows.Threading;
using System.Xml.Serialization;

namespace Omega_Red.Managers
{
    class MemoryCardManager: IManager
    {

        class Compare : IEqualityComparer<MemoryCardInfo>
        {
            public bool Equals(MemoryCardInfo x, MemoryCardInfo y)
            {
                if (x.FileName == y.FileName)
                {
                    return true;
                }
                else { return false; }
            }
            public int GetHashCode(MemoryCardInfo codeh)
            {
                return 0;
            }


        }

        private const int MCD_SIZE = 1024 * 8 * 16;		// Legacy PSX card default size

        private const int MC2_MBSIZE = 1024 * 528 * 2;		// Size of a single megabyte of card data

        private const int MC2_SIZE = MC2_MBSIZE * 8;		// PS2 card default size (8MB)

        private byte[] m_effeffs = new byte[528 * 16];

        private List<int> m_ListSlotIndexes = new List<int>();

        private bool m_GoogleDriveAccess = false;

        private string m_disk_serial = "";

        private ICollectionView mCustomerView = null;

        private static MemoryCardManager m_Instance = null;

        public static MemoryCardManager Instance { get { if (m_Instance == null) m_Instance = new MemoryCardManager(); return m_Instance; } }

        private MemoryCardManager() {
            
            for (int i = 0; i < m_effeffs.Length; i++)
            {
                m_effeffs[i] = 0xff;
            }

            if (string.IsNullOrEmpty(Settings.Default.MemoryCardsFolder))
                Settings.Default.MemoryCardsFolder = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments) + @"\OmegaRed\memcards\";

            if (!System.IO.Directory.Exists(Settings.Default.MemoryCardsFolder))
            {
                System.IO.Directory.CreateDirectory(Settings.Default.MemoryCardsFolder);
            }

            mCustomerView = CollectionViewSource.GetDefaultView(_memoryCardInfoCollection);
                       
            mCustomerView.SortDescriptions.Add(
            new SortDescription("DateTime", ListSortDirection.Descending));
            
            mCustomerView.CurrentChanged += mCustomerView_CurrentChanged;

            PCSX2Controller.Instance.ChangeStatusEvent += Instance_ChangeStatusEvent;


            GoogleAccountManager.Instance.mEnableStateEvent += (obj) =>
            {
                m_GoogleDriveAccess = obj;

                m_disk_serial = "";

                if (PCSX2Controller.Instance.IsoInfo != null)
                    load();
            };
        }

        void Instance_ChangeStatusEvent(PCSX2Controller.StatusEnum a_Status)
        {
            if (a_Status != PCSX2Controller.StatusEnum.NoneInitilized)
                load();
        }

        void mCustomerView_CurrentChanged(object sender, EventArgs e)
        {            
            var l_MemoryCardInfo = mCustomerView.CurrentItem as MemoryCardInfo;

            if (l_MemoryCardInfo != null)
            {
                if(l_MemoryCardInfo.IsCloudOnlysave)
                {
                    Application.Current.Dispatcher.BeginInvoke(DispatcherPriority.Background, (ThreadStart)delegate ()
                    {
                        mCustomerView.MoveCurrentToPosition(-1);
                    });
                }
                else
                    selectMemoryCardInfo(l_MemoryCardInfo);
            }
        }

        private readonly ObservableCollection<MemoryCardInfo> _memoryCardInfoCollection = new ObservableCollection<MemoryCardInfo>();
        
        public void load()
        {
            if (PCSX2Controller.Instance.IsoInfo == null)
                return;

            if (PCSX2Controller.Instance.IsoInfo.GameType == GameType.PSP)
                return;

            if (m_disk_serial == PCSX2Controller.Instance.IsoInfo.DiscSerial)
                return;

            m_disk_serial = PCSX2Controller.Instance.IsoInfo.DiscSerial;

            _memoryCardInfoCollection.Clear();

            m_ListSlotIndexes.Clear();

            for (int i = 0; i < 100; i++)
            {
                m_ListSlotIndexes.Add(i);
            }

            if (System.IO.Directory.Exists(Settings.Default.MemoryCardsFolder))
            {
                var l_SelectedMemoryCardFile = PCSX2Controller.Instance.IsoInfo.SelectedMemoryCardFile;

                string[] files = System.IO.Directory.GetFiles(Settings.Default.MemoryCardsFolder, "*.ps2");

                foreach (string s in files)
                {
                    // Create the FileInfo object only when needed to ensure
                    // the information is as current as possible.
                    System.IO.FileInfo fi = null;
                    try
                    {
                        fi = new System.IO.FileInfo(s);
                    }
                    catch (System.IO.FileNotFoundException e)
                    {
                        // To inform the user and continue is
                        // sufficient for this demonstration.
                        // Your application may require different behavior.
                        Console.WriteLine(e.Message);
                        continue;
                    }

                    var l_splits = fi.Name.Split(new char[] { '.' });

                    if (l_splits != null && l_splits.Length == 3)
                    {
                        if (fi.Name.Contains(PCSX2Controller.Instance.IsoInfo.DiscSerial))
                        {
                            int l_value = 0;

                            if (int.TryParse(l_splits[1], out l_value))
                            {
                                m_ListSlotIndexes.Remove(l_value);

                                addMemoryCardInfo(new MemoryCardInfo()
                                {
                                    Visibility = System.Windows.Visibility.Visible,
                                    Index = l_value,
                                    FileName = fi.Name,
                                    FilePath = fi.FullName,
                                    DateTime = fi.LastWriteTime,
                                    Date = fi.LastWriteTime.ToString("dd/MM/yyyy HH:mm:ss")
                                });
                            }
                        }
                    }
                    //else
                    //    addMemoryCardInfo(new MemoryCardInfo()
                    //    {
                    //        Visibility = System.Windows.Visibility.Hidden,
                    //        Index = -1,
                    //        FileName = fi.Name,
                    //        FilePath = fi.FullName,
                    //        DateTime = fi.LastWriteTime,
                    //        Date = fi.LastWriteTime.ToString("dd/MM/yyyy HH:mm:ss")
                    //});
                }

                fetchCloudMemoryCard(PCSX2Controller.Instance.IsoInfo.DiscSerial);

                bool l_selected = false;

                foreach (var item in _memoryCardInfoCollection)
                {
                    if(item.FileName == l_SelectedMemoryCardFile)
                    {
                        selectMemoryCardInfo(item);

                        l_selected = true;

                        break;
                    }
                }

                if(!l_selected && _memoryCardInfoCollection.Count > 0)
                    selectMemoryCardInfo(_memoryCardInfoCollection[0]);

                Application.Current.Dispatcher.BeginInvoke(DispatcherPriority.Background, (ThreadStart)delegate ()
                {
                    foreach (var item in _memoryCardInfoCollection)
                    {
                        if (item.IsCurrent)
                        {
                            mCustomerView.MoveCurrentTo(item);

                            break;
                        }
                    }
                });
            }
        }

        private void addMemoryCardInfo(MemoryCardInfo aMemoryCardInfo)
        {
            if (!_memoryCardInfoCollection.Contains(aMemoryCardInfo, new Compare()))
            {
                _memoryCardInfoCollection.Add(aMemoryCardInfo);
            }
        }

        private void addMemoryCardInfo()
        {
            if (PCSX2Controller.Instance.IsoInfo == null)
                return;

            if (m_ListSlotIndexes.Count > 0)
            {
                var lIndexString = m_ListSlotIndexes[0].ToString();

                if (lIndexString.Length == 1)
                    lIndexString = lIndexString.PadLeft(2, '0');

                var Name = (PCSX2Controller.Instance.IsoInfo.Title + "-" + PCSX2Controller.Instance.IsoInfo.DiscSerial) + "." + lIndexString + ".ps2";

                var FullName = Settings.Default.MemoryCardsFolder + Name;

                try
                {
                    using (var fs = new FileStream(FullName, FileMode.Create, FileAccess.Write, FileShare.None))
                    {
                        for (uint i = 0; i < (MC2_SIZE) / m_effeffs.Length; i++)
                        {
                            try
                            {
                                fs.Write(m_effeffs, 0, m_effeffs.Length);
                            }
                            catch (Exception)
                            {
                                return;
                            }
                        }
                    }
                }
                catch (Exception)
                {
                    Name = PCSX2Controller.Instance.IsoInfo.DiscSerial + "." + lIndexString + ".ps2";

                    FullName = Settings.Default.MemoryCardsFolder + Name;

                    using (var fs = new FileStream(FullName, FileMode.Create, FileAccess.Write, FileShare.None))
                    {
                        for (uint i = 0; i < (MC2_SIZE) / m_effeffs.Length; i++)
                        {
                            try
                            {
                                fs.Write(m_effeffs, 0, m_effeffs.Length);
                            }
                            catch (Exception)
                            {
                                return;
                            }
                        }
                    }
                }

                addMemoryCardInfo(new MemoryCardInfo()
                {
                    Visibility = System.Windows.Visibility.Visible,
                    Index = m_ListSlotIndexes[0],
                    FileName = Name,
                    FilePath = FullName,
                    DateTime = DateTime.Now,
                    Date = DateTime.Now.ToString("dd/MM/yyyy HH:mm:ss")
                });

                m_ListSlotIndexes.RemoveAt(0);
            }
        }

        private void fetchCloudMemoryCard(string a_disk_serial)
        {
            if (m_GoogleDriveAccess)
            {
                var l_MemoryCardList = DriveManager.Instance.getMemoryCardList(a_disk_serial);

                foreach (var item in l_MemoryCardList)
                {
                    if (m_ListSlotIndexes.Contains(item.Index))
                    {
                        addMemoryCardInfo(item);

                        m_ListSlotIndexes.Remove(item.Index);
                    }
                    else
                    {
                        try
                        {
                            var l_memoryCard = _memoryCardInfoCollection.ToList().First(memoryCard => memoryCard.Index == item.Index);

                            if (l_memoryCard != null)
                            {
                                l_memoryCard.IsCloudsave = true;
                            }
                        }
                        catch (Exception)
                        {
                        }
                    }
                }
            }
        }

        public void setMemoryCard()
        {
            bool l_isSelected = false;

            foreach (var l_item in _memoryCardInfoCollection)
            {
                if(l_item.IsCurrent)
                {
                    l_isSelected = true;

                    break;
                }
            }

            if (l_isSelected)
                return;

            foreach (var l_item in _memoryCardInfoCollection)
            {
                if (!l_item.IsCloudOnlysave)
                {
                    l_isSelected = true;

                    Application.Current.Dispatcher.BeginInvoke(DispatcherPriority.Background, (ThreadStart)delegate ()
                    {
                        mCustomerView.MoveCurrentTo(l_item);
                    });

                    break;
                }
            }

            if (l_isSelected)
                return;

            addMemoryCardInfo();

            foreach (var l_item in _memoryCardInfoCollection)
            {
                if (!l_item.IsCloudOnlysave)
                {
                    l_isSelected = true;

                    Application.Current.Dispatcher.BeginInvoke(DispatcherPriority.Background, (ThreadStart)delegate ()
                    {
                        mCustomerView.MoveCurrentTo(l_item);
                    });

                    break;
                }
            }
        }

        private void removeMemoryCardInfo(MemoryCardInfo aMemoryCardInfo)
        {
            if (_memoryCardInfoCollection.Contains(aMemoryCardInfo, new Compare()))
            {
                _memoryCardInfoCollection.Remove(aMemoryCardInfo);

                if (aMemoryCardInfo.IsCurrent)
                {
                    ModuleControl.Instance.setMemoryCard();
                }

                File.Delete(aMemoryCardInfo.FilePath);

                if (aMemoryCardInfo.Index >= 0)
                    m_ListSlotIndexes.Insert(0, aMemoryCardInfo.Index);

                aMemoryCardInfo.IsCurrent = false;

                if (aMemoryCardInfo.IsCloudsave)
                {
                    aMemoryCardInfo.IsCloudOnlysave = true;

                    fetchCloudMemoryCard(m_disk_serial);
                }
            }
        }

        private void selectMemoryCardInfo(MemoryCardInfo aMemoryCardInfo)
        {
            if (aMemoryCardInfo.IsCloudOnlysave)
                return;

            foreach (var item in _memoryCardInfoCollection)
            {
                item.IsCurrent = false;
            }

            aMemoryCardInfo.IsCurrent = true;

            ModuleControl.Instance.setMemoryCard(aMemoryCardInfo.FilePath);

            if (PCSX2Controller.Instance.IsoInfo != null)
            {
                PCSX2Controller.Instance.IsoInfo.SelectedMemoryCardFile = aMemoryCardInfo.FileName;

                IsoManager.Instance.save();
            }
        }
        
        public async void removeItem(object a_Item)
        {
            var l_MemoryCardInfo = a_Item as MemoryCardInfo;

            if (l_MemoryCardInfo != null)
            {
                if (l_MemoryCardInfo.IsCloudOnlysave)
                {
                    await DriveManager.Instance.startDeletingMemoryCardAsync(l_MemoryCardInfo.FilePath);

                    if (_memoryCardInfoCollection.Contains(l_MemoryCardInfo, new Compare()))
                    {
                        _memoryCardInfoCollection.Remove(l_MemoryCardInfo);

                        if (l_MemoryCardInfo.Index >= 0)
                            m_ListSlotIndexes.Insert(0, l_MemoryCardInfo.Index);
                    }
                }
                else
                    removeMemoryCardInfo(l_MemoryCardInfo);
            }
        }

        public void createItem()
        {
            addMemoryCardInfo();
        }

        public async void persistItemAsync(object a_Item)
        {
            var l_Grid = a_Item as System.Windows.Controls.Grid;

            var l_MemoryCardInfo = l_Grid.DataContext as MemoryCardInfo;

            if (l_MemoryCardInfo != null)
            {
                string lDescription = "Date=" + l_MemoryCardInfo.Date;

                var lProgressBannerGrid = l_Grid.FindName("mProgressBannerBorder") as System.Windows.FrameworkElement;


                var l_TempFileName = System.IO.Path.GetTempPath() + l_MemoryCardInfo.FileName; 

                File.Copy(l_MemoryCardInfo.FilePath, l_TempFileName, true);


                if (lProgressBannerGrid != null)
                    await DriveManager.Instance.startUploadingMemoryCardAsync(l_TempFileName, lProgressBannerGrid, lDescription);

                try
                {
                    File.Delete(l_TempFileName);
                }
                catch (Exception)
                {
                }

                l_Grid.DataContext = null;

                l_MemoryCardInfo.IsCloudsave = true;

                l_Grid.DataContext = l_MemoryCardInfo;
            }
        }

        public async void loadItemAsync(object a_Item)
        {
            if (string.IsNullOrEmpty(Settings.Default.MemoryCardsFolder))
                return;

            var l_Grid = a_Item as System.Windows.Controls.Grid;

            var l_MemoryCardInfo = l_Grid.DataContext as MemoryCardInfo;

            if (l_MemoryCardInfo != null)
            {
                if (l_MemoryCardInfo.IsCurrent)
                {
                    ModuleControl.Instance.setMemoryCard();
                }

                var lProgressBannerGrid = l_Grid.FindName("mProgressBannerBorder") as System.Windows.FrameworkElement;

                if (lProgressBannerGrid != null)
                    await DriveManager.Instance.startDownloadingMemoryCardAsync(
                        l_MemoryCardInfo.FilePath,
                        lProgressBannerGrid);

                l_MemoryCardInfo.IsCloudsave = true;

                l_MemoryCardInfo.IsCloudOnlysave = false;

                if (_memoryCardInfoCollection.Contains(l_MemoryCardInfo, new Compare()))
                {
                    _memoryCardInfoCollection.Remove(l_MemoryCardInfo);
                }

                addMemoryCardInfo(l_MemoryCardInfo);

                m_ListSlotIndexes.Remove(l_MemoryCardInfo.Index);

                if (l_MemoryCardInfo.IsCurrent)
                {
                    mCustomerView.MoveCurrentTo(l_MemoryCardInfo);
                }
            }
        }

        public bool accessPersistItem(object a_Item)
        {
            var l_Grid = a_Item as System.Windows.Controls.Grid;

            var l_MemoryCardInfo = l_Grid.DataContext as MemoryCardInfo;

            if (l_MemoryCardInfo != null)
            {
                return !l_MemoryCardInfo.IsCloudOnlysave;
            }

            return true;
        }

        public bool accessLoadItem(object a_Item)
        {
            var l_Grid = a_Item as System.Windows.Controls.Grid;

            var l_MemoryCardInfo = l_Grid.DataContext as MemoryCardInfo;

            if (l_MemoryCardInfo != null)
            {
                return l_MemoryCardInfo.IsCloudsave;
            }

            return false;
        }

        public ICollectionView Collection
        {
            get { return mCustomerView; }
        }
    }
}
